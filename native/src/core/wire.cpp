#include "core/wire.h"

#include <algorithm>

#include "zlib.h"

namespace tcr {
namespace wire {

uint32_t crc32(const uint8_t* data, size_t n) {
  uLong crc = ::crc32(0L, Z_NULL, 0);
  while (n > 0) {
    uInt take = n > 0x40000000u ? 0x40000000u : static_cast<uInt>(n);
    crc = ::crc32(crc, data, take);
    data += take;
    n -= take;
  }
  return static_cast<uint32_t>(crc);
}

void put_u16(Bytes& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

void put_u32(Bytes& out, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
}

void put_u64(Bytes& out, uint64_t v) {
  for (int s = 56; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
}

uint32_t get_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 |
         p[3];
}

uint64_t get_u64(const uint8_t* p) { return static_cast<uint64_t>(get_u32(p)) << 32 | get_u32(p + 4); }

Bytes frame(uint32_t type_id, const Bytes& body) {
  Bytes out;
  out.reserve(12 + body.size());
  put_u32(out, static_cast<uint32_t>(12 + body.size()));
  put_u32(out, type_id);
  put_u32(out, crc32(body));
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

Bytes blob(const Bytes& data) {
  Bytes out;
  out.reserve(4 + data.size());
  put_u32(out, static_cast<uint32_t>(data.size()));
  out.insert(out.end(), data.begin(), data.end());
  return out;
}

Bytes empty_array() {
  Bytes count;
  put_u32(count, 0);
  return blob(frame(T_ARRAY, count));
}

Bytes u64_array(const std::vector<uint64_t>& words) {
  Bytes body;
  put_u32(body, static_cast<uint32_t>(words.size()));
  for (uint64_t w : words) put_u64(body, w);
  return blob(frame(T_ARRAY, body));
}

Bytes serializer_list(const std::vector<Bytes>& elems) {
  Bytes body;
  for (const Bytes& e : elems) {
    Bytes b = blob(e);
    body.insert(body.end(), b.begin(), b.end());
  }
  return frame(T_LIST, body);
}

Parsed parse_message(const uint8_t* plain, size_t n, Message* out) {
  if (n < 12) return Parsed::kNone;
  Message m;
  m.length = get_u32(plain);
  m.type = get_u32(plain + 4);
  uint32_t crc = get_u32(plain + 8);
  size_t len = std::min<size_t>(m.length, n);
  const uint8_t* msg = plain;
  m.crc_ok = len == m.length && crc32(msg + std::min<size_t>(12, len), len > 12 ? len - 12 : 0) == crc;
  if (m.type == T_DATA && m.length >= 33) {
    if (len < 12 + 17) return Parsed::kError;
    m.transfer = get_u32(msg + 12);
    m.channel = get_u32(msg + 16);
    m.flags = msg[20];
    m.chunk = get_u32(msg + 21);
    m.total = get_u32(msg + 25);
    m.payload = Bytes(msg + std::min<size_t>(33, len), msg + len);
  } else if (m.type == T_ACK && m.length >= 49) {
    if (len < 12 + 13) return Parsed::kError;
    m.transfer = get_u32(msg + 12);
    m.channel = get_u32(msg + 16);
    m.flags = msg[20];
    m.kind = get_u32(msg + 21);
    if (len < 49) return Parsed::kError;
    m.stamp = Bytes(msg + 25, msg + 33);
    m.received = get_u32(msg + 45);
  }
  *out = std::move(m);
  return Parsed::kOk;
}

Bytes data_chunk(uint32_t transfer, uint32_t channel, uint8_t flags, const Bytes& payload, uint32_t index,
                 uint32_t total) {
  Bytes body;
  put_u32(body, transfer);
  put_u32(body, channel);
  body.push_back(flags);
  put_u32(body, index);
  put_u32(body, total);
  Bytes b = blob(payload);
  body.insert(body.end(), b.begin(), b.end());
  return frame(T_DATA, body);
}

std::vector<Bytes> data_chunks(uint32_t transfer, uint32_t channel, uint8_t flags, const Bytes& inner,
                               size_t chunk) {
  std::vector<Bytes> out;
  size_t total = inner.size();
  for (size_t i = 0; i <= total / chunk; i++) {
    size_t a = std::min(i * chunk, total), b = std::min((i + 1) * chunk, total);
    out.push_back(data_chunk(transfer, channel, flags, Bytes(inner.begin() + static_cast<std::ptrdiff_t>(a),
                                                             inner.begin() + static_cast<std::ptrdiff_t>(b)),
                             static_cast<uint32_t>(i), static_cast<uint32_t>(total)));
  }
  return out;
}

Bytes ack(uint32_t transfer, uint32_t channel, uint8_t flags, uint64_t stamp, uint32_t received) {
  Bytes body;
  put_u32(body, transfer);
  put_u32(body, channel);
  body.push_back(flags);
  put_u32(body, 1);
  put_u64(body, stamp);
  put_u64(body, 0);
  put_u32(body, 0);
  put_u32(body, received);
  return frame(T_ACK, body);
}

std::optional<Bytes> ack_confirm(const Message& a) {
  if (!a.transfer || !a.channel || !a.flags || !a.kind || !a.stamp || !a.received) return std::nullopt;
  Bytes body;
  put_u32(body, *a.transfer);
  put_u32(body, *a.channel);
  body.push_back(*a.flags);
  put_u32(body, *a.kind);
  body.insert(body.end(), a.stamp->begin(), a.stamp->end());
  put_u32(body, *a.received);
  return frame(T_ACK_CONFIRM, body);
}

std::optional<std::array<uint8_t, 4>> inet_aton(std::string_view ip) {
  if (ip.find('\0') != std::string_view::npos) return std::nullopt;
  auto at = [&](size_t i) -> unsigned char { return i < ip.size() ? static_cast<unsigned char>(ip[i]) : 0; };
  auto isdig = [](unsigned char c) { return c >= '0' && c <= '9'; };
  auto isxdig = [&](unsigned char c) { return isdig(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
  auto isspc = [](unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
  uint64_t parts[3] = {0, 0, 0};
  size_t np = 0, i = 0;
  uint64_t val = 0;
  bool digit = false;
  unsigned char c = at(i);
  while (true) {
    if (!isdig(c)) return std::nullopt;
    val = 0;
    uint64_t base = 10;
    digit = false;
    if (c == '0') {
      c = at(++i);
      if (c == 'x' || c == 'X') {
        base = 16;
        c = at(++i);
      } else {
        base = 8;
        digit = true;
      }
    }
    while (true) {
      if (isdig(c)) {
        if (base == 8 && (c == '8' || c == '9')) return std::nullopt;
        val = val * base + (c - '0');
        c = at(++i);
        digit = true;
      } else if (base == 16 && isxdig(c)) {
        val = (val << 4) | static_cast<uint64_t>(c + 10 - (c >= 'a' ? 'a' : 'A'));
        c = at(++i);
        digit = true;
      } else {
        break;
      }
    }
    if (c == '.') {
      if (np >= 3 || val > 0xff) return std::nullopt;
      parts[np++] = val;
      c = at(++i);
    } else {
      break;
    }
  }
  if (c != 0 && !isspc(c)) return std::nullopt;
  if (!digit) return std::nullopt;
  switch (np + 1) {
    case 2:
      if (val > 0xffffff) return std::nullopt;
      val |= parts[0] << 24;
      break;
    case 3:
      if (val > 0xffff) return std::nullopt;
      val |= (parts[0] << 24) | (parts[1] << 16);
      break;
    case 4:
      if (val > 0xff) return std::nullopt;
      val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
      break;
    default:
      break;
  }
  uint32_t v = static_cast<uint32_t>(val);
  return std::array<uint8_t, 4>{static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16),
                                static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
}

std::optional<Bytes> resolve_reply(const Bytes& request_body, std::string_view ip, int64_t port) {
  std::optional<std::array<uint8_t, 4>> addr = inet_aton(ip);
  if (!addr || port < 0 || port > 65535) return std::nullopt;
  Bytes body(request_body.begin(), request_body.begin() + static_cast<std::ptrdiff_t>(
                                                              std::min<size_t>(13, request_body.size())));
  body.insert(body.end(), addr->begin(), addr->end());
  put_u16(body, static_cast<uint16_t>(port));
  return frame(T_RESOLVE_REPLY, body);
}

std::shared_ptr<PendingEntry> Partial::get(const PartialKey& key) const {
  for (const auto& e : entries)
    if (e.first == key) return e.second;
  return nullptr;
}

void Partial::erase(const PartialKey& key) {
  for (size_t i = 0; i < entries.size(); i++) {
    if (entries[i].first == key) {
      entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
      return;
    }
  }
}

void PendingRegistry::charge(const std::shared_ptr<Partial>& home, const PartialKey& key,
                             const std::shared_ptr<PendingEntry>& entry, int64_t cost) {
  auto it = entry->serial ? held_.find(entry->serial) : held_.end();
  if (it == held_.end()) {
    entry->serial = next_serial_++;
    it = held_.emplace(entry->serial, Record{home, key, entry, 0}).first;
  }
  it->second.cost += cost;
  held_bytes_ += cost;
}

void PendingRegistry::release(const std::shared_ptr<PendingEntry>& entry) {
  auto it = entry->serial ? held_.find(entry->serial) : held_.end();
  if (it == held_.end()) return;
  Record record = std::move(it->second);
  held_.erase(it);
  held_bytes_ -= record.cost;
  if (record.home && record.home->get(record.key) == entry) record.home->erase(record.key);
}

void PendingRegistry::release_all(Partial& partial) {
  std::vector<std::shared_ptr<PendingEntry>> entries;
  for (const auto& e : partial.entries) entries.push_back(e.second);
  for (const auto& e : entries) release(e);
  partial.entries.clear();
}

std::pair<std::optional<Bytes>, size_t> PendingRegistry::assemble(const std::shared_ptr<Partial>& partial,
                                                                  const Message& msg, size_t chunk) {
  const uint64_t total = *msg.total, index = *msg.chunk;
  const Bytes& payload = *msg.payload;
  if (total > MAX_REQUEST || index > total / chunk || payload.size() > chunk) return {std::nullopt, 0};
  PartialKey key{*msg.transfer, *msg.channel};
  std::shared_ptr<PendingEntry> entry = partial->get(key);
  if (!entry) {
    while (partial->size() >= max_pending_) {
      PartialKey oldest = partial->entries.front().first;
      std::shared_ptr<PendingEntry> oldest_entry = partial->entries.front().second;
      release(oldest_entry);
      partial->erase(oldest);
    }
    entry = std::make_shared<PendingEntry>();
    partial->entries.emplace_back(key, entry);
    charge(partial, key, entry, 512);
  }
  auto old = entry->parts.find(static_cast<uint32_t>(index));
  bool had = old != entry->parts.end();
  int64_t grown = static_cast<int64_t>(payload.size()) - (had ? static_cast<int64_t>(old->second.size()) : 0);
  entry->parts[static_cast<uint32_t>(index)] = payload;
  entry->held += grown;
  charge(partial, key, entry, grown + (had ? 0 : 128));
  if (entry->held < static_cast<int64_t>(total)) {
    while (held_bytes_ > max_bytes_ && !held_.empty()) {
      std::shared_ptr<PendingEntry> first = held_.begin()->second.entry;
      release(first);
    }
    return {std::nullopt, 0};
  }
  release(entry);
  Bytes whole;
  for (const auto& p : entry->parts) whole.insert(whole.end(), p.second.begin(), p.second.end());
  if (whole.size() > total) whole.resize(total);
  return {std::move(whole), entry->parts.size()};
}

}
}
