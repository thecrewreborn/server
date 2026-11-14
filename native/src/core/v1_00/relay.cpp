#include "core/v1_00/relay.h"

#include <algorithm>
#include <set>

#include "core/core.h"
#include "core/keys.h"
#include "core/v1_00/crypto.h"
#include "core/v1_00/handlers.h"
#include "core/wire.h"

namespace tcr {
namespace v1_00 {

namespace {

using v1_21::Addr;
using v1_21::DatagramResult;
using v1_21::Packet;
using v1_21::Session;

const uint32_t kGuestRequests[] = {
    0x036D7773,
    0x27C711C5,
    0x2E0E24CB,
    0x368D58C9,
    0x40BD5E2B,
    0x720CC96E,
    0xB1F54E5C,
};

json::Value str(const std::string& s) { return json::Value::string(s); }

Bytes concat(const Bytes& a, const Bytes& b) {
  Bytes out = a;
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

Raised os_error_aton() { return Raised{"OSError", "illegal IP address string passed to inet_aton"}; }

bool addr_bytes(const std::string& ip, int port, Bytes& out) {
  std::optional<std::array<uint8_t, 4>> a = wire::inet_aton(ip);
  if (!a) return false;
  out.insert(out.end(), a->begin(), a->end());
  wire::put_u16(out, static_cast<uint16_t>(port));
  return true;
}

std::string chunk_key(const Addr& addr, uint32_t transfer, uint32_t channel, uint32_t chunk) {
  return addr.key() + std::string(1, '\0') + std::to_string(transfer) + ":" + std::to_string(channel) + ":" +
         std::to_string(chunk);
}

const std::vector<Bytes>* cached_reply(const OrderedMap<std::vector<Bytes>>& replies, uint32_t transfer,
                                       uint32_t channel) {
  auto on = [channel](const std::vector<Bytes>* chunks) {
    return chunks && !chunks->empty() && (*chunks)[0].size() >= 20 &&
           wire::get_u32((*chunks)[0].data() + 16) == channel;
  };
  const std::vector<Bytes>* chunks = replies.get(std::to_string(channel) + ":" + std::to_string(transfer));
  if (on(chunks)) return chunks;
  const std::string suffix = ":" + std::to_string(transfer);
  for (auto it = replies.end(); it != replies.begin();) {
    --it;
    const std::string& key = it->first;
    bool same = key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                key.find(':') == key.size() - suffix.size();
    if (same && on(&it->second)) return &it->second;
  }
  return nullptr;
}

v1_21::RelayOptions with_dlc_none(v1_21::RelayOptions o) {
  o.dlc = "none";
  return o;
}

}

Relay::Relay(Core& core, v1_21::RelayOptions options, v1_21::RelayHost* host)
    : v1_21::Relay(core, with_dlc_none(std::move(options)), host), key_(kHandshakeKey, kHandshakeKey + 32) {
  get_player_ = GET_OR_CREATE_PLAYER;
  chunk_ = CHUNK;
}

std::string Relay::rpc_name(uint32_t id) const { return v1_00::name_of(id); }

v1_21::HandleResult Relay::dispatch(v1_21::Ctx& ctx, uint32_t id, const Bytes& body) {
  return v1_00::handle(ctx, id, body);
}

std::optional<Bytes> Relay::seal(const Session& sess, const Bytes& plain, Raised* raised) {
  Bytes out;
  if (!crypto::encrypt(sess.key, plain, &out, raised)) return std::nullopt;
  return out;
}

bool Relay::guest_request(uint32_t rpc) const {
  return std::find(std::begin(kGuestRequests), std::end(kGuestRequests), rpc) != std::end(kGuestRequests);
}

void Relay::say(const Addr& addr, const std::string& text, bool always) {
  v1_21::Relay::say(addr, "[1.00] " + text, always);
}

void Relay::note(OrderedMap<double>& table, const std::string& key, double now, double window) {
  table.set(key, now);
  table.move_to_end(key);
  while (!table.empty()) {
    double first = table.front().second;
    if (now - first < window && table.size() <= TABLE_MAX) break;
    table.pop_front();
  }
}

void Relay::probe(const Addr& addr, DatagramResult* r) {
  new_session(addr);
  std::string ip;
  Raised raised;
  if (!public_ip(addr, &ip, &raised)) {
    r->raised = raised;
    return;
  }
  Bytes plain;
  if (!addr_bytes(ip, o_.port, plain) || !addr_bytes(addr.ip, addr.port, plain)) {
    r->raised = os_error_aton();
    return;
  }
  wire::put_u32(plain, 1);
  wire::put_u32(plain, 1);
  Bytes enc;
  if (!crypto::encrypt(key_, plain, &enc, &raised)) {
    r->raised = raised;
    return;
  }
  Bytes reply = concat(Bytes(6, 0), enc);
  say(addr, "01.00 probe -> handshake 1 (" + std::to_string(reply.size()) + "B)");
  json::Value e = json::Value::object();
  e.set("dir", str("out"));
  e.set("dst", str(addr.text()));
  e.set("step", json::Value::integer(1));
  e.set("hex", str(py::hex(reply)));
  log(std::move(e));
  r->packets.push_back(Packet{std::move(reply), addr});
}

void Relay::key_exchange(const Addr& addr, DatagramResult* r) {
  Session& sess = new_session(addr);
  sess.key.assign(32, 0);
  core_.env().urandom(sess.key.data(), 32);
  sess.iv.clear();
  sess.connected = true;
  muted_ = false;
  Bytes enc;
  Raised raised;
  if (!crypto::encrypt(key_, sess.key, &enc, &raised)) {
    r->raised = raised;
    return;
  }
  Bytes reply = concat(Bytes{0, 0, 0, 0, 0, 1}, enc);
  say(addr, "01.00 key request -> handshake 2 (" + std::to_string(reply.size()) + "B)");
  json::Value e = json::Value::object();
  e.set("dir", str("out"));
  e.set("dst", str(addr.text()));
  e.set("step", json::Value::integer(2));
  e.set("hex", str(py::hex(reply)));
  e.set("session_key", str(py::hex(sess.key)));
  log(std::move(e));
  r->packets.push_back(Packet{std::move(reply), addr});
}

void Relay::tunnel(Session& sess, const Bytes& header, const Bytes& body, const Addr& addr, DatagramResult* r) {
  Bytes plain;
  Raised raised;
  if (!crypto::decrypt(sess.key, body, &plain, &raised)) {
    r->raised = raised;
    return;
  }
  wire::Message msg;
  wire::Parsed parsed = wire::parse_message(plain.data(), plain.size(), &msg);
  if (parsed == wire::Parsed::kError) {
    tunnel_plain(sess, header, plain, addr, r);
    return;
  }
  bool ok = parsed == wire::Parsed::kOk && msg.crc_ok;
  if (ok && msg.type == T_NACK) {
    size_t end = std::min<size_t>(std::max<size_t>(12, msg.length), plain.size());
    resend(sess, header, Bytes(plain.begin() + 12, plain.begin() + static_cast<std::ptrdiff_t>(end)), addr, r);
    return;
  }
  bool request = ok && msg.type == wire::T_DATA && msg.flags && (*msg.flags & 1);
  if (request) {
    std::string key = chunk_key(addr, *msg.transfer, *msg.channel, *msg.chunk);
    double now = clock();
    const double* seen = seen_.get(key);
    if (seen && now - *seen < DUPLICATE_WINDOW) return;
    note(seen_, key, now, DUPLICATE_WINDOW);
  }
  size_t before = r->packets.size();
  tunnel_plain(sess, header, plain, addr, r);
  if (r->raised) return;
  if (ok && msg.type == wire::T_DATA && msg.flags && (*msg.flags & 3) == 1) {
    std::vector<Packet> kept(r->packets.begin(), r->packets.begin() + static_cast<std::ptrdiff_t>(before));
    for (size_t i = before; i < r->packets.size(); i++) {
      const Packet& pkt = r->packets[i];
      Bytes sent(pkt.data.begin() + std::min<size_t>(6, pkt.data.size()), pkt.data.end()), back;
      if (!crypto::decrypt(sess.key, sent, &back, &raised)) {
        r->raised = raised;
        return;
      }
      wire::Message m;
      wire::Parsed p = wire::parse_message(back.data(), back.size(), &m);
      if (p == wire::Parsed::kNone || (p == wire::Parsed::kOk && m.type != wire::T_DATA)) kept.push_back(pkt);
    }
    if (kept.size() < r->packets.size()) {
      say(addr, "transfer " + std::to_string(*msg.transfer) + ": request sent with flags 1, its reply is not sent");
    }
    r->packets = std::move(kept);
  }
}

void Relay::resend(Session& sess, const Bytes& header, const Bytes& body, const Addr& addr, DatagramResult* r) {
  if (body.size() < 14) return;
  uint32_t transfer = wire::get_u32(body.data()), channel = wire::get_u32(body.data() + 4);
  const std::vector<Bytes>* found = cached_reply(sess.replies, transfer, channel);
  if (!found) return;
  const std::vector<Bytes>& chunks = *found;
  size_t n = wire::get_u32(body.data() + 10);
  Bytes array(body.begin() + 14, body.begin() + static_cast<std::ptrdiff_t>(std::min(body.size(), 14 + n)));
  size_t count = array.size() >= 16 ? wire::get_u32(array.data() + 12) : 0;
  std::vector<uint32_t> words;
  if (array.size() >= 16 + 4 * count) {
    for (size_t k = 0; k < count; k++) words.push_back(wire::get_u32(array.data() + 16 + 4 * k));
  }
  std::set<size_t> missing;
  size_t k = 0;
  while (k < words.size()) {
    uint32_t w = words[k];
    if ((w & 0x80000000u) && k + 1 < words.size()) {
      size_t stop = std::min<size_t>(static_cast<size_t>(words[k + 1]) + 1, chunks.size());
      for (size_t i = w & 0x7FFFFFFFu; i < stop; i++) missing.insert(i);
      k += 2;
    } else {
      if ((w & 0x7FFFFFFFu) < chunks.size()) missing.insert(w & 0x7FFFFFFFu);
      k += 1;
    }
  }
  double now = clock();
  std::vector<size_t> due;
  for (size_t i : missing) {
    const double* last = resent_.get(chunk_key(addr, transfer, channel, static_cast<uint32_t>(i)));
    if (!last || now - *last >= RESEND_WINDOW) due.push_back(i);
  }
  if (due.empty() || !bytes_ok(addr) || !may_request(addr)) return;
  int64_t total = 0;
  for (size_t i : due) total += static_cast<int64_t>(chunks[i].size());
  charge_bytes(addr, total);
  size_t sent = 0;
  for (size_t i : due) {
    note(resent_, chunk_key(addr, transfer, channel, static_cast<uint32_t>(i)), now, RESEND_WINDOW);
    Raised raised;
    std::optional<Bytes> sealed = seal(sess, chunks[i], &raised);
    if (!sealed) {
      r->packets.clear();
      r->raised = raised;
      return;
    }
    r->packets.push_back(Packet{concat(header, *sealed), addr});
    sent++;
  }
  if (sent) {
    say(addr, "NACK transfer " + std::to_string(transfer) + " -> resent " + std::to_string(sent) + " of " +
                  std::to_string(chunks.size()) + " chunk(s)");
    json::Value e = json::Value::object();
    e.set("dir", str("out"));
    e.set("dst", str(addr.text()));
    e.set("kind", str("resend"));
    e.set("transfer", json::Value::integer(transfer));
    json::Value list = json::Value::array();
    for (size_t i : missing) list.push(json::Value::integer(static_cast<int64_t>(i)));
    e.set("chunks", std::move(list));
    log(std::move(e));
  }
}

}
}
