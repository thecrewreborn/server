#include "core/v1_21/relay.h"

#include <algorithm>
#include <cstdio>

#include "core/core.h"
#include "core/crypto_aes.h"
#include "core/keys.h"
#include "core/logtext.h"
#include "core/ubi.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/peers.h"
#include "core/v1_21/replies.h"
#include "core/v1_21/summit.h"

namespace tcr {
namespace v1_21 {

namespace {

const uint32_t kNoAckChannels[] = {0x49466F25, 0x5ABA06B7};
const uint32_t kPlaintextRequests[] = {0x997BED03, 0xEA3A66D7, 0x9101DE70, 0x368D58C9};
const uint32_t kGuestRequests[] = {0x036D7773, 0x27C711C5, 0x368D58C9, 0x40BD5E2B, 0x720CC96E,
                                   0x9AA22640, 0xA1155286, 0xB1F54E5C, 0xCD3B60F2, 0xD14A2746};
const char kTicketMark[] = "b2ZmbGluZS10aWNrZXQ6";

template <size_t N>
bool in(const uint32_t (&set)[N], uint32_t v) {
  return std::find(set, set + N, v) != set + N;
}

std::string request_key(uint32_t rpc, uint32_t transfer) {
  return std::to_string(rpc) + ":" + std::to_string(transfer);
}

int64_t total_size(const std::vector<Bytes>& chunks) {
  int64_t n = 0;
  for (const Bytes& c : chunks) n += static_cast<int64_t>(c.size());
  return n;
}

Bytes concat(const Bytes& a, const Bytes& b) {
  Bytes out = a;
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

json::Value str(const std::string& s) { return json::Value::string(s); }

std::string hex8(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%#010x", v);
  return buf;
}

Raised os_error_aton() { return Raised{"OSError", "illegal IP address string passed to inet_aton"}; }

std::string decode_replace(const Bytes& raw) {
  std::string out;
  size_t pos = 0;
  while (pos < raw.size()) {
    std::string part;
    py::DecodeError err;
    if (py::decode_utf8(raw.data() + pos, raw.size() - pos, false, &part, &err)) {
      out += part;
      break;
    }
    std::string head;
    py::DecodeError ignored;
    py::decode_utf8(raw.data() + pos, err.start, false, &head, &ignored);
    out += head;
    py::append_utf8(out, 0xFFFD);
    pos += std::max(err.end, err.start + 1);
  }
  return out;
}

Raised parse_error(const uint8_t* plain, size_t n) {
  uint32_t length = wire::get_u32(plain), type = wire::get_u32(plain + 4);
  size_t msg = std::min<size_t>(length, n);
  if (type == wire::T_DATA) {
    return pack::error("unpack_from requires a buffer of at least 29 bytes for unpacking 17 bytes at offset 12 "
                       "(actual buffer size is " + std::to_string(msg) + ")");
  }
  if (msg < 25) {
    return pack::error("unpack_from requires a buffer of at least 25 bytes for unpacking 13 bytes at offset 12 "
                       "(actual buffer size is " + std::to_string(msg) + ")");
  }
  return pack::error("unpack_from requires a buffer of at least 49 bytes for unpacking 4 bytes at offset 45 "
                     "(actual buffer size is " + std::to_string(msg) + ")");
}

std::string isoformat(const LocalTime& t) {
  char buf[48];
  if (t.microsecond) {
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06d", t.year, t.month, t.day, t.hour, t.minute,
                  t.second, t.microsecond);
  } else {
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute,
                  t.second);
  }
  return buf;
}

}

std::string Addr::key() const {
  return ip + std::string(1, '\0') + (port_text.empty() ? std::to_string(port) : port_text);
}
std::string Addr::text() const { return ip + ":" + (port_text.empty() ? std::to_string(port) : port_text); }

bool RelayHost::append_log(const std::string&, const std::string&, std::string*) { return true; }
void RelayHost::log_failed(const std::string&, const std::string&) {}
bool RelayHost::local_ip_for(const std::string&, std::string* ip, Raised*) {
  *ip = "127.0.0.1";
  return true;
}

Bytes hide_tickets(const Bytes& data) {
  Bytes out = data;
  const size_t mark = sizeof(kTicketMark) - 1;
  size_t i = 0;
  while (i + mark <= out.size()) {
    if (std::equal(kTicketMark, kTicketMark + mark, out.begin() + static_cast<std::ptrdiff_t>(i))) {
      size_t j = i + mark;
      while (j < out.size()) {
        uint8_t c = out[j];
        bool b64 = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
                   c == '/' || c == '=';
        if (!b64) break;
        j++;
      }
      std::fill(out.begin() + static_cast<std::ptrdiff_t>(i), out.begin() + static_cast<std::ptrdiff_t>(j), '*');
      i = j;
    } else {
      i++;
    }
  }
  return out;
}

namespace {

std::string shown_hex(const Bytes& data) {
  std::string full = py::hex(hide_tickets(data));
  if (full.size() <= kLogHex) return full;
  return full.substr(0, kLogHex) + "... <" + std::to_string(data.size()) + " bytes>";
}

}

std::optional<std::string> player_name(const Bytes& body) {
  if (body.size() < 0x30) return std::nullopt;
  uint32_t n = wire::get_u32(body.data() + 0x2C);
  size_t end = std::min(body.size(), 0x30 + static_cast<size_t>(n));
  Bytes raw(body.begin() + 0x30, body.begin() + static_cast<std::ptrdiff_t>(std::max<size_t>(end, 0x30)));
  if (!(0 < n && n <= 64 && raw.size() == n)) return std::nullopt;
  return decode_replace(raw);
}

std::optional<std::string> player_ticket(const Bytes& body) {
  if (body.size() < 0x30) return std::nullopt;
  uint32_t n = wire::get_u32(body.data() + 0x2C);
  size_t at = 0x30 + static_cast<size_t>(n);
  if (!(0 < n && n <= 64) || body.size() < at + 5) return std::nullopt;
  uint32_t size = wire::get_u32(body.data() + at + 1);
  size_t a = at + 5, b = std::min(body.size(), at + 5 + static_cast<size_t>(size));
  std::string raw(body.begin() + static_cast<std::ptrdiff_t>(a), body.begin() + static_cast<std::ptrdiff_t>(b));
  if (!(0 < size && size <= 128 && raw.size() == size)) return std::nullopt;
  for (char c : raw) {
    if (static_cast<uint8_t>(c) >= 0x80) return std::nullopt;
  }
  return raw;
}

Relay::Relay(Core& core, RelayOptions options, RelayHost* host)
    : core_(core),
      o_(std::move(options)),
      host_(host ? host : &default_host_),
      guest_lines_("relay port " + std::to_string(o_.port) + ": requests before a verified GET_PLAYER"),
      evicted_lines_("relay port " + std::to_string(o_.port) + ": resends of replies no longer cached") {
  if (o_.proxy_key.empty()) o_.proxy_key.assign(kHandshakeKey, kHandshakeKey + 32);
  if (o_.proxy_iv.empty()) o_.proxy_iv.assign(kHandshakeIv, kHandshakeIv + 16);
  Files& files = *core.files();
  if (!o_.save_path.empty()) {
    std::string abs;
    OsError err;
    if (!files.abspath(o_.save_path, &abs, &err)) {
      start_error_ = Raised{"FileNotFoundError", err.text()};
      return;
    }
    store_.reset(new session::SessionStore(files, files.join(files.dirname(abs), session::STORE_NAME)));
  }
  say_window_ = clock();
  log_window_ = clock();
  Raised raised;
  if (!new_ctx(kDefaultName, "", &ctx_, &raised)) {
    start_error_ = raised;
    return;
  }
  restore();
}

Relay::~Relay() = default;

std::vector<std::pair<Addr, const Session*>> Relay::sessions() const {
  std::vector<std::pair<Addr, const Session*>> out;
  for (const auto& e : sessions_) out.emplace_back(e.second.addr, e.second.sess.get());
  return out;
}

std::string Relay::rpc_name(uint32_t id) const { return name_of(id); }

HandleResult Relay::dispatch(Ctx& ctx, uint32_t id, const Bytes& body) { return handle(ctx, id, body); }

std::optional<Bytes> Relay::seal(const Session& sess, const Bytes& plain, Raised* raised) {
  std::optional<Bytes> out = aes::encrypt(sess.key, sess.iv, plain);
  if (!out) {
    if (sess.key.size() != 16 && sess.key.size() != 24 && sess.key.size() != 32) {
      *raised = Raised{"ValueError", "Invalid key size (" + std::to_string(sess.key.size() * 8) + ") for AES."};
    } else {
      *raised = Raised{"ValueError", "Invalid IV size (" + std::to_string(sess.iv.size()) + ") for CBC."};
    }
  }
  return out;
}

bool Relay::guest_request(uint32_t rpc) const { return in(kGuestRequests, rpc); }

double Relay::clock() { return core_.env().monotonic(); }

std::string Relay::ts() {
  LocalTime t = core_.env().now_local();
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", t.hour, t.minute, t.second, t.microsecond / 1000);
  return buf;
}

void Relay::print(const std::string& line) { core_.env().print_line(line); }

Session* Relay::find(const Addr& addr) const {
  const Entry* e = sessions_.get(addr.key());
  return e ? e->sess.get() : nullptr;
}

const Session* Relay::session(const Addr& addr) const { return find(addr); }

void Relay::restore() {
  if (!store_) return;
  session::Loaded loaded = store_->load(core_.env().time());
  if (loaded.error) {
    size_t colon = loaded.error->find(": ");
    start_error_ = Raised{loaded.error->substr(0, colon), loaded.error->substr(colon + 2)};
    return;
  }
  size_t kept = 0;
  for (const session::Stored& s : loaded.sessions) {
    if (!truthy(s.name)) continue;
    Addr a(s.ip, 0);
    std::optional<int64_t> port = py::int_text_to_i64(s.port);
    if (port && *port >= INT32_MIN && *port <= INT32_MAX) {
      a.port = static_cast<int>(*port);
    } else {
      a.port_text = s.port;
    }
    auto sess = std::make_shared<Session>();
    sess->key = s.key;
    sess->iv = s.iv;
    sess->connected = true;
    if (s.name.is_string()) {
      sess->name = s.name.as_string();
    } else {
      sess->name = obj::str(s.name);
      sess->odd_name = s.name;
    }
    sess->platform = s.platform;
    sess->restored = true;
    sess->stamp = s.stamp;
    sess->seen = clock();
    stored_.insert(a.key());
    sessions_.set(a.key(), Entry{a, sess});
    kept++;
  }
  if (kept) {
    print("[" + ts() + "] relay port " + std::to_string(o_.port) + ": " + std::to_string(kept) +
          " game session(s) from before the restart kept");
  }
}

void Relay::remember(Session* sess, const Addr& addr) {
  if (!(store_ && sess)) return;
  sess->stamp = core_.env().time();
  if (sess->name || stored_.count(addr.key())) store_named();
}

Session& Relay::new_session(const Addr& addr) {
  auto sess = std::make_shared<Session>();
  sess->key.resize(32);
  core_.env().urandom(sess->key.data(), 32);
  sess->iv.resize(16);
  core_.env().urandom(sess->iv.data(), 16);
  sess->seen = clock();
  drop(addr);
  if (OrderedMap<Addr>* mine = guests_by_ip_.get(addr.ip)) {
    while (mine && mine->size() >= MAX_GUESTS_PER_IP) {
      const Addr* pick = nullptr;
      for (auto& g : *mine) {
        if (unproven_.contains(g.first)) {
          pick = &g.second;
          break;
        }
      }
      Addr victim = pick ? *pick : mine->front().second;
      drop(victim);
      mine = guests_by_ip_.get(addr.ip);
    }
  }
  while (guests_.size() >= MAX_GUESTS) drop(!unproven_.empty() ? unproven_.front().second : guests_.front().second);
  sessions_.set(addr.key(), Entry{addr, sess});
  guest_active(addr);
  return *sess;
}

void Relay::guest_active(const Addr& addr) {
  const std::string k = addr.key();
  guests_.set(k, addr);
  guests_.move_to_end(k);
  OrderedMap<Addr>& mine = guests_by_ip_.setdefault(addr.ip);
  mine.set(k, addr);
  mine.move_to_end(k);
  Session* sess = find(addr);
  std::shared_ptr<Session>* proven = proven_.get(k);
  if (sess == nullptr || !proven || proven->get() != sess) {
    unproven_.set(k, addr);
    unproven_.move_to_end(k);
  }
}

void Relay::unguest(const Addr& addr) {
  const std::string k = addr.key();
  guests_.erase(k);
  unproven_.erase(k);
  if (OrderedMap<Addr>* mine = guests_by_ip_.get(addr.ip)) {
    mine->erase(k);
    if (mine->empty()) guests_by_ip_.erase(addr.ip);
  }
}

void Relay::expire(double now) {
  if (now - swept_ < 1) return;
  swept_ = now;
  std::vector<const Ctx*> in_use;
  for (auto& e : sessions_) {
    if (e.second.sess->ctx) in_use.push_back(e.second.sess->ctx.get());
  }
  std::vector<Addr> idle;
  bool named = false;
  for (auto& e : sessions_) {
    const Session& s = *e.second.sess;
    if (now - s.seen > (s.name ? NAMED_IDLE_S : GUEST_IDLE_S)) {
      idle.push_back(e.second.addr);
      named = named || bool(s.name);
    }
  }
  for (auto& g : guests_) {
    if (!sessions_.contains(g.first)) idle.push_back(g.second);
  }
  for (const Addr& a : idle) drop(a);
  if (named) store_named();
  std::vector<std::string> keys;
  for (auto& p : players_) keys.push_back(p.first);
  for (const std::string& key : keys) {
    const Ctx* ctx = players_.get(key)->get();
    if (std::find(in_use.begin(), in_use.end(), ctx) != in_use.end()) {
      player_seen_.set(key, now);
    } else {
      double* seen = player_seen_.get(key);
      if (!seen) seen = &player_seen_.set(key, now);
      if (now - *seen > PLAYER_IDLE_S) {
        players_.erase(key);
        player_seen_.erase(key);
      }
    }
  }
}

void Relay::drop(const Addr& addr) {
  unguest(addr);
  forget_session(addr);
  const std::string k = addr.key();
  if (std::shared_ptr<wire::Partial>* pending = direct_partial_.get(k)) {
    std::shared_ptr<wire::Partial> p = *pending;
    direct_partial_.erase(k);
    if (p->size()) core_.pending().release_all(*p);
  }
  proven_.erase(k);
}

void Relay::forget_session(const Addr& addr) {
  const std::string k = addr.key();
  Entry* e = sessions_.get(k);
  std::shared_ptr<Session> old = e ? e->sess : nullptr;
  if (e) sessions_.erase(k);
  if (old && old->partial->size()) core_.pending().release_all(*old->partial);
  guest_caches_.erase(k);
  player_caches_.erase(k);
  if (old) reply_total_ -= old->reply_bytes;
}

namespace {

void store_reply(Session& sess, const std::string& key, const std::vector<Bytes>& chunks, size_t count, int64_t size,
                 bool keep_oversized) {
  if (std::vector<Bytes>* old = sess.replies.get(key)) {
    sess.reply_bytes -= total_size(*old);
    sess.replies.erase(key);
  }
  if (!keep_oversized && total_size(chunks) > size) return;
  sess.replies.set(key, chunks);
  sess.reply_bytes += total_size(chunks);
  while (sess.replies.size() > 1 && (sess.replies.size() > count || sess.reply_bytes > size)) {
    sess.reply_bytes -= total_size(sess.replies.front().second);
    sess.replies.pop_front();
  }
}

}

void Relay::cache_reply(Session& sess, const Addr& addr, const std::string& key, const std::vector<Bytes>& chunks) {
  int64_t before = sess.reply_bytes;
  if (sess.name) {
    store_reply(sess, key, chunks, REPLY_CACHE, REPLY_BYTES, true);
  } else {
    store_reply(sess, key, chunks, GUEST_REPLY_CACHE, GUEST_REPLY_BYTES, false);
  }
  reply_total_ += sess.reply_bytes - before;
  OrderedMap<Addr>& mine = sess.name ? player_caches_ : guest_caches_;
  OrderedMap<Addr>& others = sess.name ? guest_caches_ : player_caches_;
  const std::string k = addr.key();
  others.erase(k);
  if (!sess.replies.empty()) {
    mine.set(k, addr);
    mine.move_to_end(k);
  } else {
    mine.erase(k);
  }
  while (reply_total_ > REPLY_BYTES_TOTAL) {
    OrderedMap<Addr>* table = nullptr;
    for (OrderedMap<Addr>* t : {&guest_caches_, &player_caches_}) {
      bool other = false;
      for (auto& a : *t) other = other || a.first != k;
      if (other) {
        table = t;
        break;
      }
    }
    if (!table) break;
    std::string oldest;
    for (auto& a : *table) {
      if (a.first != k) {
        oldest = a.first;
        break;
      }
    }
    table->erase(oldest);
    if (Entry* e = sessions_.get(oldest)) {
      reply_total_ -= e->sess->reply_bytes;
      e->sess->replies.clear();
      e->sess->reply_bytes = 0;
    }
  }
}

void Relay::store_named() {
  if (!store_) return;
  std::vector<session::Stored> kept;
  stored_.clear();
  for (auto& e : sessions_) {
    const Session& s = *e.second.sess;
    if (!s.name) continue;
    stored_.insert(e.first);
    if (!s.connected) continue;
    session::Stored st;
    st.ip = e.second.addr.ip;
    st.port = e.second.addr.port_text.empty() ? std::to_string(e.second.addr.port) : e.second.addr.port_text;
    st.key = s.key;
    st.iv = s.iv;
    st.name = s.odd_name ? *s.odd_name : json::Value::string(*s.name);
    st.platform = s.platform;
    st.stamp = s.stamp;
    kept.push_back(std::move(st));
  }
  if (std::optional<std::string> line = store_->save(kept)) print(*line);
}

bool Relay::new_ctx(const std::string& name, const std::string& path, std::shared_ptr<Ctx>* out, Raised* raised) {
  Files& files = *core_.files();
  bool fresh = !(!path.empty() && files.exists(path));
  auto ctx = std::make_shared<Ctx>();
  ctx->core = &core_;
  ctx->player_name = name;
  ctx->player_id = player_id_for(name);
  std::optional<Player> p = Player::load(files, path, reward_max_bucks(core_), raised);
  if (!p) return false;
  ctx->player = std::move(*p);
  if (o_.abilities == "all" && !truthy(ctx->player.abilities)) {
    ctx->player.abilities = obj::list_of_u64({ALL_ABILITIES[0], ALL_ABILITIES[1]});
    if (std::optional<Raised> failed = ctx->player.save()) {
      *raised = *failed;
      return false;
    }
  }
  ctx->player.unlocks = unlock_fields(o_.unlocks);
  ctx->player.dlc = dlc_fields(o_.dlc);
  if (!start_crew_credits(core_, ctx->player, fresh, raised)) return false;
  *out = std::move(ctx);
  return true;
}

bool start_crew_credits(Core& core, Player& p, bool fresh, Raised* raised) {
  config::Result<bool> refill = core.config().crew_credits_refill();
  if (!refill.ok()) {
    *raised = Raised{refill.error.type, refill.error.message};
    return false;
  }
  auto credits = [&](int64_t* into) {
    config::Result<std::string> c = core.config().crew_credits();
    if (!c.ok()) {
      *raised = Raised{c.error.type, c.error.message};
      return false;
    }
    std::optional<int64_t> v = py::int_text_to_i64(*c.value);
    *into = v ? *v : INT64_MAX;
    return true;
  };
  if (*refill.value) return credits(&p.cc_fixed);
  p.cc_fixed = 0;
  return !fresh || credits(&p.crew_credits);
}

bool Relay::ctx_for_name(const std::string& name, std::shared_ptr<Ctx>* out, Raised* raised) {
  if (name.empty() || name == kDefaultName || o_.save_path.empty()) {
    *out = ctx_;
    return true;
  }
  const std::string key = account_key(name);
  if (!players_.contains(key)) {
    Files& files = *core_.files();
    std::string abs;
    OsError err;
    if (!files.abspath(o_.save_path, &abs, &err)) {
      *raised = Raised{"FileNotFoundError", err.text()};
      return false;
    }
    std::string folder = files.join(files.dirname(abs), "players");
    std::string path = files.join(folder, save_name(name) + ".json");
    std::shared_ptr<Ctx> ctx;
    if (!new_ctx(name, path, &ctx, raised)) return false;
    players_.set(key, ctx);
  }
  player_seen_.set(key, clock());
  *out = *players_.get(key);
  return true;
}

bool Relay::ctx_for(const Addr& addr, Session* sess, std::shared_ptr<Ctx>* out, Raised* raised) {
  if (!sess) sess = find(addr);
  if (sess && sess->ctx) {
    *out = sess->ctx;
    return true;
  }
  if (!sess || !sess->name) {
    std::shared_ptr<Ctx> ctx;
    if (!new_ctx(kDefaultName, "", &ctx, raised)) return false;
    if (sess) sess->ctx = ctx;
    *out = ctx;
    return true;
  }
  std::shared_ptr<Ctx> ctx;
  if (sess->odd_name && !o_.save_path.empty() && *sess->name != kDefaultName) {
    *raised = Raised{"TypeError", std::string("expected string or bytes-like object, got '") +
                                      obj::type_name(*sess->odd_name) + "'"};
    return false;
  }
  if (!ctx_for_name(*sess->name, &ctx, raised)) return false;
  ctx->verified = true;
  ctx->client_ip = addr.ip;
  const std::string* known = core_.ubi().session_platform(addr.ip);
  bool uplay = truthy(sess->platform) ? (sess->platform.is_string() && sess->platform.as_string() == "uplay")
                                      : (known && *known == "uplay");
  ctx->pc = uplay;
  sess->ctx = ctx;
  *out = ctx;
  return true;
}

bool Relay::public_ip(const Addr& peer, std::string* out, Raised* raised) {
  config::Result<std::optional<std::string>> advertised = core_.config().advertised_ip();
  if (!advertised.ok()) {
    *raised = Raised{advertised.error.type, advertised.error.message};
    return false;
  }
  if (*advertised.value) {
    *out = **advertised.value;
    return true;
  }
  if (o_.ip != "0.0.0.0") {
    *out = o_.ip;
    return true;
  }
  return host_->local_ip_for(peer.ip, out, raised);
}

void Relay::log(json::Value entry) {
  if (o_.log_path.empty() || muted_) return;
  if (!may_log()) return;
  bool step2 = false;
  if (const json::Value* step = entry.get("step")) step2 = step->is_int() && step->as_i64() == 2;
  json::Value kept = json::Value::object();
  for (const json::Member& m : entry.members()) {
    if (m.key == "session_key" || m.key == "session_iv" || (m.key == "hex" && step2)) continue;
    kept.set(m.key, m.value);
  }
  for (const char* k : {"hex", "plain", "payload"}) {
    const json::Value* v = kept.get(k);
    if (!v || !v->is_string()) continue;
    std::string full = py::hex(hide_tickets(py::fromhex(v->as_string()).value_or(Bytes())));
    kept.set(k, str(full.substr(0, std::min(full.size(), kLogHex))));
    if (full.size() > kLogHex) kept.set(std::string(k) + "_bytes", json::Value::integer(int64_t(full.size() / 2)));
  }
  kept.set("time", str(isoformat(core_.env().now_local())));
  std::string why;
  if (!host_->append_log(o_.log_path, json::dumps(kept) + "\n", &why)) host_->log_failed(o_.log_path, why);
}

void Relay::send_failed(const Addr& to, const std::string& error) { say(to, "cannot send a reply: " + error); }

void Relay::handler_error(const Bytes& data, const Addr& from, const std::string& traceback) {
  Bytes head(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(data.size(), 32)));
  say(from, "handler error on " + py::hex(head) + "\n" + traceback);
  json::Value e = json::Value::object();
  e.set("dir", str("in"));
  e.set("src", str(from.text()));
  e.set("kind", str("error"));
  e.set("hex", str(py::hex(data)));
  e.set("error", str(traceback));
  log(std::move(e));
}

bool Relay::may_log() {
  Session* sess = current_ ? find(*current_) : nullptr;
  if (sess && sess->name) return true;
  double now = clock();
  if (now - log_window_ >= SAY_WINDOW_S) {
    if (log_left_out_) {
      json::Value e = json::Value::object();
      e.set("kind", str("suppressed"));
      e.set("entries", json::Value::integer(log_left_out_));
      e.set("time", str(isoformat(core_.env().now_local())));
      std::string why;
      if (!host_->append_log(o_.log_path, json::dumps(e) + "\n", &why)) host_->log_failed(o_.log_path, why);
    }
    log_window_ = now;
    log_count_ = 0;
    log_left_out_ = 0;
  }
  if (log_count_ >= LOG_UNVERIFIED_PER_MIN) {
    log_left_out_++;
    return false;
  }
  log_count_++;
  return true;
}

void Relay::guest_dropped(const Addr& addr, const std::string& text) {
  std::string suppressed;
  bool allowed = guest_lines_.allow(addr.ip, clock(), &suppressed);
  if (!suppressed.empty()) print(stamped(core_.env(), suppressed));
  if (allowed) say(addr, text, true);
}

void Relay::evicted_dropped(const Addr& addr, const std::string& text) {
  std::string suppressed;
  bool allowed = evicted_lines_.allow(addr.ip, clock(), &suppressed);
  if (!suppressed.empty()) print(stamped(core_.env(), suppressed));
  if (allowed) say(addr, text, true);
}

void Relay::say(const Addr& addr, const std::string& text, bool always) {
  if (!always && !may_say(addr)) return;
  print("[" + ts() + "] " + addr.text() + " " + text);
}

bool Relay::may_say(const Addr& addr) {
  double now = clock();
  if (now - say_window_ >= SAY_WINDOW_S) {
    if (say_left_out_) {
      print("[" + ts() + "] relay port " + std::to_string(o_.port) + ": " + std::to_string(say_left_out_) +
            " line(s) from unverified senders suppressed");
    }
    say_window_ = now;
    say_count_ = 0;
    say_left_out_ = 0;
  }
  Session* sess = find(addr);
  if (sess && sess->name) return true;
  while (!said_.empty() && now - said_.front().second >= SAY_WINDOW_S) said_.pop_front();
  if (said_.contains(addr.key()) || say_count_ >= SAY_UNVERIFIED_PER_MIN) {
    say_left_out_++;
    return false;
  }
  said_.set(addr.key(), now);
  say_count_++;
  return true;
}

Relay::Budget& Relay::budget(const std::string& ip) {
  double now = clock();
  Budget* b = budgets_.get(ip);
  if (!b) {
    b = &budgets_.set(ip, Budget{REQUEST_BURST, REPLY_BYTES_BURST, now});
  } else {
    double gone = std::max(0.0, now - b->at);
    b->tokens = std::min(REQUEST_BURST, b->tokens + gone * REQUESTS_PER_S);
    b->reply_bytes = std::min(REPLY_BYTES_BURST, b->reply_bytes + gone * REPLY_BYTES_PER_S);
    b->at = now;
  }
  budgets_.move_to_end(ip);
  while (budgets_.size() > BUDGET_ADDRESSES || now - budgets_.front().second.at > BUDGET_IDLE_S) budgets_.pop_front();
  return *budgets_.get(ip);
}

bool Relay::may_request(const Addr& addr) {
  Budget& b = budget(addr.ip);
  if (b.tokens < 1) return false;
  b.tokens -= 1;
  return true;
}

bool Relay::bytes_ok(const Addr& addr) { return budget(addr.ip).reply_bytes >= 0; }

void Relay::charge_bytes(const Addr& addr, int64_t n) {
  if (n) budget(addr.ip).reply_bytes -= static_cast<double>(n);
}

std::optional<std::vector<Bytes>> Relay::ack_for(const wire::Message& msg, size_t received, const Ctx& ctx,
                                                 Raised* raised) {
  if (in(kNoAckChannels, *msg.channel)) return std::vector<Bytes>{};
  std::optional<int64_t> stamp = ctx.client_now_us(raised);
  if (!stamp) return std::nullopt;
  if (*stamp < 0) {
    *raised = pack::error("'Q' format requires 0 <= number <= 18446744073709551615");
    return std::nullopt;
  }
  return std::vector<Bytes>{wire::ack(*msg.transfer, *msg.channel, *msg.flags, static_cast<uint64_t>(*stamp),
                                      static_cast<uint32_t>(received))};
}

bool Relay::reply_chunks(const wire::Message& msg, uint32_t rpc, const Bytes& request, Ctx& ctx,
                         std::optional<std::vector<Bytes>>* chunks, DatagramResult* result) {
  Bytes body(request.begin() + 12, request.end());
  HandleResult h = dispatch(ctx, rpc, body);
  switch (h.kind) {
    case HandleResult::kRaised: {
      if (!caught_by_reply_chunks(h.raised)) {
        result->raised = h.raised;
        return false;
      }
      json::Value e = json::Value::object();
      e.set("kind", str("handler_error"));
      e.set("rpc", str(hex8(rpc)));
      e.set("error", str(pack::exception_repr(h.raised)));
      e.set("payload", str(py::hex(request)));
      log(std::move(e));
      chunks->reset();
      return true;
    }
    case HandleResult::kUnknown:
      chunks->reset();
      return true;
    case HandleResult::kNoReply:
      *chunks = std::vector<Bytes>();
      return true;
    case HandleResult::kFrame:
      *chunks = wire::data_chunks(*msg.transfer, *msg.channel, wire::REPLY_FLAGS, h.frame, chunk_);
      return true;
  }
  return true;
}

DatagramResult Relay::handle_datagram(const Bytes& data, const Addr& addr) {
  DatagramResult r;
  if (addr.ip == o_.ip && addr.port == o_.port) return r;
  Bytes header(data.begin(), data.begin() + std::min<size_t>(6, data.size()));
  Bytes body(data.begin() + std::min<size_t>(6, data.size()), data.end());
  Session* known = find(addr);
  current_ = addr;
  muted_ = !(known && known->connected);
  double now = clock();
  expire(now);
  bool handshake = data.size() == 6 && header[4] == 0 && (header[5] == 0 || header[5] == 1);
  if (handshake && !handshakes_.allow(addr.ip, now)) return r;
  if (guests_.contains(addr.key())) guest_active(addr);
  {
    json::Value e = json::Value::object();
    e.set("dir", str("in"));
    e.set("src", str(addr.text()));
    e.set("hex", str(py::hex(data)));
    log(std::move(e));
  }
  if (handshake && header[5] == 0) {
    probe(addr, &r);
    return r;
  }
  if (handshake && header[5] == 1) {
    key_exchange(addr, &r);
    if (r.raised) return r;
    unknown_.erase(addr.key());
    remember(find(addr), addr);
    return r;
  }
  if (header == Bytes(6, 0xFF)) {
    direct(header, body, addr, &r);
    return r;
  }
  Session* sess = find(addr);
  if (!(sess && sess->connected && !body.empty())) {
    json::Value e = json::Value::object();
    e.set("dir", str("in"));
    e.set("src", str(addr.text()));
    e.set("kind", str("unknown"));
    log(std::move(e));
    if (!body.empty() && !unknown_.count(addr.key())) {
      if (unknown_.size() >= MAX_UNKNOWN) unknown_.clear();
      unknown_.insert(addr.key());
      say(addr, "packets of a game session this server does not know (no key): dropped until the game connects again");
    }
    return r;
  }
  sess->seen = clock();
  for (OrderedMap<Addr>* caches : {&guest_caches_, &player_caches_}) {
    if (caches->contains(addr.key())) caches->move_to_end(addr.key());
  }
  if (sess->restored) {
    sess->restored = false;
    say(addr, "game session from before the restart goes on" + (sess->name ? " (player " + *sess->name + ")" : ""));
  }
  tunnel(*sess, header, body, addr, &r);
  return r;
}

void Relay::probe(const Addr& addr, DatagramResult* r) {
  new_session(addr);
  std::string ip;
  Raised raised;
  if (!public_ip(addr, &ip, &raised)) {
    r->raised = raised;
    return;
  }
  std::optional<std::array<uint8_t, 4>> server = wire::inet_aton(ip);
  if (!server) {
    r->raised = os_error_aton();
    return;
  }
  Bytes plain(server->begin(), server->end());
  wire::put_u16(plain, static_cast<uint16_t>(o_.port));
  std::optional<std::array<uint8_t, 4>> client = wire::inet_aton(addr.ip);
  if (!client) {
    r->raised = os_error_aton();
    return;
  }
  plain.insert(plain.end(), client->begin(), client->end());
  wire::put_u16(plain, static_cast<uint16_t>(addr.port));
  wire::put_u32(plain, 1);
  wire::put_u32(plain, 1);
  std::optional<Bytes> enc = aes::encrypt(o_.proxy_key, o_.proxy_iv, plain);
  if (!enc) {
    r->raised = Raised{"ValueError", "Invalid key size (" + std::to_string(o_.proxy_key.size() * 8) + ") for AES."};
    return;
  }
  Bytes reply = concat(Bytes(6, 0), *enc);
  say(addr, "probe -> handshake 1 (" + std::to_string(reply.size()) + "B)");
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
  sess.iv.assign(16, 0);
  core_.env().urandom(sess.iv.data(), 16);
  sess.connected = true;
  muted_ = false;
  std::optional<Bytes> enc = aes::encrypt(o_.proxy_key, o_.proxy_iv, concat(sess.key, sess.iv));
  if (!enc) {
    r->raised = Raised{"ValueError", "Invalid key size (" + std::to_string(o_.proxy_key.size() * 8) + ") for AES."};
    return;
  }
  Bytes reply = concat(Bytes{0, 0, 0, 0, 0, 1}, *enc);
  say(addr, "key request -> handshake 2 (" + std::to_string(reply.size()) + "B)");
  json::Value e = json::Value::object();
  e.set("dir", str("out"));
  e.set("dst", str(addr.text()));
  e.set("step", json::Value::integer(2));
  e.set("hex", str(py::hex(reply)));
  log(std::move(e));
  r->packets.push_back(Packet{std::move(reply), addr});
}

void Relay::direct(const Bytes& header, const Bytes& body, const Addr& addr, DatagramResult* r) {
  const std::string peer = addr.text();
  Session* sess = find(addr);
  std::shared_ptr<Session>* proven = proven_.get(addr.key());
  if (!sess || !proven || proven->get() != sess) return;
  {
    json::Value e = json::Value::object();
    e.set("dir", str("in"));
    e.set("src", str(peer));
    e.set("kind", str("direct"));
    e.set("payload", str(py::hex(body)));
    log(std::move(e));
  }
  wire::Message msg;
  wire::Parsed parsed = wire::parse_message(body.data(), body.size(), &msg);
  if (parsed == wire::Parsed::kError) {
    r->raised = parse_error(body.data(), body.size());
    return;
  }
  if (parsed == wire::Parsed::kNone || !msg.crc_ok) return;
  if (!(msg.type == wire::T_DATA && msg.flags && (*msg.flags & 1))) return;
  const std::string k = addr.key();
  if (!direct_partial_.contains(k) && direct_partial_.size() >= MAX_UNKNOWN) {
    for (auto& pending : direct_partial_) core_.pending().release_all(*pending.second);
    direct_partial_.clear();
  }
  std::shared_ptr<wire::Partial>* partial = direct_partial_.get(k);
  if (!partial) partial = &direct_partial_.set(k, std::make_shared<wire::Partial>());
  std::pair<std::optional<Bytes>, size_t> got = core_.pending().assemble(*partial, msg, chunk_);
  if (!got.first || got.first->size() < 12) return;
  const Bytes& request = *got.first;
  uint32_t rpc = wire::get_u32(request.data() + 4);
  if (!in(kPlaintextRequests, rpc) || !may_request(addr)) return;
  std::shared_ptr<Ctx> guest;
  Raised raised;
  if (!new_ctx(kDefaultName, "", &guest, &raised)) {
    r->raised = raised;
    return;
  }
  std::optional<std::vector<Bytes>> chunks;
  if (!reply_chunks(msg, rpc, request, *guest, &chunks, r)) return;
  std::vector<Bytes> out;
  if (!chunks) {
    json::Value e = json::Value::object();
    e.set("dir", str("in"));
    e.set("kind", str("unhandled_rpc"));
    e.set("rpc", str(rpc_name(rpc)));
    e.set("target", str("direct"));
    e.set("payload", str(py::hex(request)));
    log(std::move(e));
  } else {
    out = *chunks;
    say(addr, "direct " + rpc_name(rpc) + " transfer " + std::to_string(*msg.transfer) + " -> plaintext reply " +
                  std::to_string(out.size()) + " chunk(s)");
  }
  for (const Bytes& c : out) {
    json::Value e = json::Value::object();
    e.set("dir", str("out"));
    e.set("dst", str(peer));
    e.set("kind", str("direct_reply"));
    e.set("plain", str(py::hex(c)));
    log(std::move(e));
  }
  Raised ack_raised;
  std::optional<std::vector<Bytes>> acks = ack_for(msg, got.second, *guest, &ack_raised);
  if (!acks) {
    r->raised = ack_raised;
    return;
  }
  for (Bytes& a : *acks) {
    json::Value e = json::Value::object();
    e.set("dir", str("out"));
    e.set("dst", str(peer));
    e.set("kind", str("direct_ack"));
    e.set("plain", str(py::hex(a)));
    log(std::move(e));
    out.push_back(std::move(a));
  }
  for (const Bytes& c : out) r->packets.push_back(Packet{concat(header, c), addr});
}

bool Relay::bind_player(Session& sess, const Addr& addr, const Bytes& request, bool* bound, DatagramResult* r) {
  *bound = true;
  Bytes body(request.begin() + 12, request.end());
  std::optional<std::string> name = player_name(body);
  if (!name || name->empty()) return true;
  std::optional<std::string> ticket = player_ticket(body);
  std::optional<std::pair<std::string, std::string>> owner;
  if (ticket && !ticket->empty()) {
    Raised raised;
    owner = core_.ubi().ticket_owner(*ticket, true, &raised);
    if (!raised.type.empty()) {
      r->raised = raised;
      return false;
    }
  }
  if (!owner || account_key(owner->first) != account_key(*name)) {
    if (!sess.refused) {
      sess.refused = true;
      say(addr, "GET_PLAYER with an unknown ticket: refused", true);
    }
    *bound = false;
    return true;
  }
  const std::string& player = owner->first;
  const std::string& platform = owner->second;
  core_.peers().forget(player_id_for(player));
  Raised raised;
  std::shared_ptr<Ctx> ctx;
  bool ok = !o_.save_path.empty() ? ctx_for_name(player, &ctx, &raised) : new_ctx(player, "", &ctx, &raised);
  if (!ok) {
    r->raised = raised;
    return false;
  }
  sess.ctx = ctx;
  ctx->pc = platform == "uplay";
  ctx->verified = true;
  ctx->client_ip = addr.ip;
  sess.name = player;
  sess.platform = json::Value::string(platform);
  unguest(addr);
  const std::string account = account_key(player);
  std::vector<Addr> others;
  for (auto& e : sessions_) {
    const Session& s = *e.second.sess;
    if (e.first == addr.key() || !s.name) continue;
    if (s.odd_name) {
      r->raised = Raised{"TypeError", std::string("expected string or bytes-like object, got '") +
                                          obj::type_name(*s.odd_name) + "'"};
      return false;
    }
    if (account_key(*s.name) == account) others.push_back(e.second.addr);
  }
  for (const Addr& a : others) drop(a);
  remember(&sess, addr);
  const char* device = platform == "uplay" ? "PC" : "PS4";
  std::string line = "[" + ts() + "] " + player + " connected from " + addr.ip + " (" + device + ")";
  const std::string& save = ctx->player.path;
  if (!save.empty()) {
    std::string rel;
    OsError err;
    if (!core_.files()->relpath(save, core_.home(), &rel, &err)) {
      r->raised = Raised{"FileNotFoundError", err.text()};
      return false;
    }
    line += ", save: " + rel;
  }
  print(line);
  return true;
}

void Relay::tunnel(Session& sess, const Bytes& header, const Bytes& body, const Addr& addr, DatagramResult* r) {
  std::optional<Bytes> plain = aes::decrypt(sess.key, sess.iv, body);
  if (!plain) {
    if (sess.key.size() != 16 && sess.key.size() != 24 && sess.key.size() != 32) {
      r->raised = Raised{"ValueError", "Invalid key size (" + std::to_string(sess.key.size() * 8) + ") for AES."};
    } else {
      r->raised = Raised{"ValueError", "Invalid IV size (" + std::to_string(sess.iv.size()) + ") for CBC."};
    }
    return;
  }
  tunnel_plain(sess, header, *plain, addr, r);
}

void Relay::tunnel_plain(Session& sess, const Bytes& header, const Bytes& plain, const Addr& addr,
                         DatagramResult* r) {
  const std::string peer = addr.text();
  char dst_ip[16];
  std::snprintf(dst_ip, sizeof(dst_ip), "%u.%u.%u.%u", header[0], header[1], header[2], header[3]);
  int dst_port = header[4] << 8 | header[5];
  const auto& services = replies::service_by_addr();
  auto svc = services.find({dst_ip, dst_port});
  const std::string dst = std::string(dst_ip) + ":" + std::to_string(dst_port);
  const std::string target = svc != services.end() ? svc->second : dst;
  wire::Message msg;
  wire::Parsed parsed = wire::parse_message(plain.data(), plain.size(), &msg);
  if (parsed == wire::Parsed::kError) {
    r->raised = parse_error(plain.data(), plain.size());
    return;
  }
  {
    json::Value e = json::Value::object();
    e.set("dir", str("in"));
    e.set("src", str(peer));
    e.set("kind", str("tunnel"));
    e.set("dst", str(dst));
    e.set("target", str(target));
    e.set("plain", str(py::hex(plain)));
    log(std::move(e));
  }
  if (parsed == wire::Parsed::kNone || !msg.crc_ok) return;
  if (sess.ctx && sess.name) {
    core_.ubi().touch(sess.ctx->player_name, addr.ip);
    core_.ubi().active(*sess.name);
  }
  std::shared_ptr<Session>* proven = proven_.get(addr.key());
  if (!proven || proven->get() != &sess) {
    Entry* e = sessions_.get(addr.key());
    proven_.set(addr.key(), e->sess);
    unproven_.erase(addr.key());
  }
  if (msg.type == wire::T_DATA && msg.flags && (*msg.flags & 1)) {
    std::pair<std::optional<Bytes>, size_t> got = core_.pending().assemble(sess.partial, msg, chunk_);
    if (!got.first || got.first->size() < 12) return;
    const Bytes& request = *got.first;
    size_t received = got.second;
    uint32_t rpc = wire::get_u32(request.data() + 4);
    if (!(bytes_ok(addr) && may_request(addr))) return;
    if (!sess.name && rpc != get_player_ && !guest_request(rpc)) {
      guest_dropped(addr, rpc_name(rpc) + " transfer " + std::to_string(*msg.transfer) +
                              " before a verified GET_PLAYER: dropped");
      json::Value e = json::Value::object();
      e.set("dir", str("in"));
      e.set("kind", str("guest_dropped"));
      e.set("rpc", str(rpc_name(rpc)));
      e.set("target", str(target));
      log(std::move(e));
      return;
    }
    const std::string key = request_key(rpc, *msg.transfer);
    std::optional<bool>* seen = sess.answered.get(key);
    bool first = seen == nullptr;
    std::optional<bool> replied = seen ? *seen : std::nullopt;
    sess.answered.set(key, replied);
    while (sess.answered.size() > REPLY_CACHE) sess.answered.pop_front();
    std::optional<std::vector<Bytes>> chunks;
    std::vector<Bytes>* cached = sess.replies.get(key);
    if (!first && cached) {
      chunks = *cached;
    } else if (replied && *replied) {
      evicted_dropped(addr, rpc_name(rpc) + " transfer " + std::to_string(*msg.transfer) +
                                " resent, its reply is no longer cached: dropped");
      return;
    } else {
      if (rpc == get_player_) {
        bool bound = true;
        if (!bind_player(sess, addr, request, &bound, r)) return;
        if (!bound) return;
      }
      std::shared_ptr<Ctx> ctx;
      Raised raised;
      if (!ctx_for(addr, &sess, &ctx, &raised)) {
        r->raised = raised;
        return;
      }
      if (!reply_chunks(msg, rpc, request, *ctx, &chunks, r)) return;
      if (chunks) {
        sess.answered.set(key, true);
        cache_reply(sess, addr, key, *chunks);
      }
    }
    std::vector<Bytes> out;
    if (!chunks) {
      if (first) {
        say(addr, "UNHANDLED RPC " + rpc_name(rpc) + " to " + target + " transfer " + std::to_string(*msg.transfer) +
                      " payload " + shown_hex(request));
      }
      json::Value e = json::Value::object();
      e.set("dir", str("in"));
      e.set("kind", str("unhandled_rpc"));
      e.set("rpc", str(rpc_name(rpc)));
      e.set("target", str(target));
      e.set("payload", str(py::hex(request)));
      log(std::move(e));
    } else {
      out = *chunks;
      if (first) {
        say(addr, rpc_name(rpc) + " from " + target + " transfer " + std::to_string(*msg.transfer) + " -> reply " +
                      std::to_string(total_size(out)) + "B in " + std::to_string(out.size()) + " chunk(s)");
      }
    }
    charge_bytes(addr, total_size(out));
    for (const Bytes& c : out) {
      json::Value e = json::Value::object();
      e.set("dir", str("out"));
      e.set("dst", str(peer));
      e.set("kind", str("rpc_reply"));
      e.set("rpc", str(rpc_name(rpc)));
      e.set("plain", str(py::hex(c)));
      log(std::move(e));
    }
    std::shared_ptr<Ctx> ack_ctx;
    Raised raised;
    if (!ctx_for(addr, nullptr, &ack_ctx, &raised)) {
      r->raised = raised;
      return;
    }
    std::optional<std::vector<Bytes>> acks = ack_for(msg, received, *ack_ctx, &raised);
    if (!acks) {
      r->raised = raised;
      return;
    }
    for (Bytes& a : *acks) {
      json::Value e = json::Value::object();
      e.set("dir", str("out"));
      e.set("dst", str(peer));
      e.set("kind", str("ack"));
      e.set("rpc", str(rpc_name(rpc)));
      e.set("plain", str(py::hex(a)));
      log(std::move(e));
      out.push_back(std::move(a));
    }
    for (const Bytes& c : out) {
      Raised raised;
      std::optional<Bytes> sealed = seal(sess, c, &raised);
      if (!sealed) {
        r->packets.clear();
        r->raised = raised;
        return;
      }
      r->packets.push_back(Packet{concat(header, *sealed), addr});
    }
    return;
  }
  if (msg.type == wire::T_ACK && msg.received) {
    std::optional<Bytes> confirm = wire::ack_confirm(msg);
    json::Value e = json::Value::object();
    e.set("dir", str("out"));
    e.set("dst", str(peer));
    e.set("kind", str("ack_confirm"));
    e.set("plain", str(py::hex(*confirm)));
    log(std::move(e));
    Raised raised;
    std::optional<Bytes> sealed = seal(sess, *confirm, &raised);
    if (!sealed) {
      r->raised = raised;
      return;
    }
    r->packets.push_back(Packet{concat(header, *sealed), addr});
    return;
  }
  if (msg.type == wire::T_RESOLVE && msg.length >= 25) {
    std::string ip;
    Raised raised;
    if (!public_ip(addr, &ip, &raised)) {
      r->raised = raised;
      return;
    }
    Bytes req(plain.begin() + std::min<size_t>(12, plain.size()),
              plain.begin() + std::min<size_t>(std::max<size_t>(12, msg.length), plain.size()));
    std::optional<Bytes> reply = wire::resolve_reply(req, ip, o_.port);
    if (!reply) {
      r->raised = os_error_aton();
      return;
    }
    say(addr, "RESOLVE transfer " + std::to_string(wire::get_u32(plain.data() + 12)) + " channel " +
                  rpc_name(wire::get_u32(plain.data() + 16)) + " -> " + ip + ":" + std::to_string(o_.port));
    json::Value e = json::Value::object();
    e.set("dir", str("out"));
    e.set("dst", str(peer));
    e.set("kind", str("resolve_reply"));
    e.set("plain", str(py::hex(*reply)));
    log(std::move(e));
    std::optional<Bytes> sealed = seal(sess, *reply, &raised);
    if (!sealed) {
      r->raised = raised;
      return;
    }
    r->packets.push_back(Packet{concat(header, *sealed), addr});
    return;
  }
  if (msg.type == wire::T_ACK_CONFIRM) return;
  if (msg.type == wire::T_DATA) {
    say(addr, "DATA (no request) to " + target + " transfer " +
                  (msg.transfer ? std::to_string(*msg.transfer) : std::string("None")) + " flags " +
                  (msg.flags ? std::to_string(*msg.flags) : std::string("None")));
    json::Value e = json::Value::object();
    e.set("dir", str("in"));
    e.set("src", str(peer));
    e.set("kind", str("data_no_request"));
    e.set("target", str(target));
    e.set("plain", str(py::hex(plain)));
    log(std::move(e));
  } else if (msg.type != wire::T_ACK) {
    char type[16];
    std::snprintf(type, sizeof(type), "0x%08X", msg.type);
    say(addr, std::string("UNKNOWN-FRAME ") + type + " to " + target + " (" + std::to_string(msg.length) + "B)");
    json::Value e = json::Value::object();
    e.set("dir", str("in"));
    e.set("src", str(peer));
    e.set("kind", str("unknown_frame"));
    e.set("target", str(target));
    e.set("plain", str(py::hex(plain)));
    log(std::move(e));
  }
}

}
}
