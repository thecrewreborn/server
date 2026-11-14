#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/util.h"

namespace tcr {
namespace wire {

constexpr uint32_t T_DATA = 0xCE57ECDE;
constexpr uint32_t T_ACK = 0xD8191434;
constexpr uint32_t T_ACK_CONFIRM = 0x32000A70;
constexpr uint32_t T_ARRAY = 0x81401D39;
constexpr uint32_t T_LIST = 0x67BEC56D;
constexpr uint32_t T_RESOLVE = 0xC7872821;
constexpr uint32_t T_RESOLVE_REPLY = 0x8048BEAC;
constexpr uint8_t REPLY_FLAGS = 0x0a;
constexpr size_t CHUNK = 1200;
constexpr uint64_t MAX_REQUEST = 65536;
constexpr size_t MAX_PENDING = 16;
constexpr int64_t MAX_PENDING_BYTES = 64ll * 1024 * 1024;

uint32_t crc32(const uint8_t* data, size_t n);
inline uint32_t crc32(const Bytes& b) { return crc32(b.data(), b.size()); }

void put_u16(Bytes& out, uint16_t v);
void put_u32(Bytes& out, uint32_t v);
void put_u64(Bytes& out, uint64_t v);
uint32_t get_u32(const uint8_t* p);
uint64_t get_u64(const uint8_t* p);

Bytes frame(uint32_t type_id, const Bytes& body);
Bytes blob(const Bytes& data);
Bytes empty_array();
Bytes u64_array(const std::vector<uint64_t>& words);
Bytes serializer_list(const std::vector<Bytes>& elems);

struct Message {
  uint32_t type = 0;
  uint32_t length = 0;
  bool crc_ok = false;
  std::optional<uint32_t> transfer, channel;
  std::optional<uint8_t> flags;
  std::optional<uint32_t> chunk, total;
  std::optional<Bytes> payload;
  std::optional<uint32_t> kind;
  std::optional<Bytes> stamp;
  std::optional<uint32_t> received;
};

enum class Parsed { kNone, kOk, kError };
Parsed parse_message(const uint8_t* plain, size_t n, Message* out);

Bytes data_chunk(uint32_t transfer, uint32_t channel, uint8_t flags, const Bytes& payload, uint32_t index,
                 uint32_t total);
std::vector<Bytes> data_chunks(uint32_t transfer, uint32_t channel, uint8_t flags, const Bytes& inner,
                               size_t chunk = CHUNK);
Bytes ack(uint32_t transfer, uint32_t channel, uint8_t flags, uint64_t stamp, uint32_t received = 1);
std::optional<Bytes> ack_confirm(const Message& ack);

std::optional<std::array<uint8_t, 4>> inet_aton(std::string_view ip);
std::optional<Bytes> resolve_reply(const Bytes& request_body, std::string_view ip, int64_t port);

using PartialKey = std::pair<uint32_t, uint32_t>;

struct PendingEntry {
  std::map<uint32_t, Bytes> parts;
  int64_t held = 0;
  uint64_t serial = 0;
};

struct Partial {
  std::vector<std::pair<PartialKey, std::shared_ptr<PendingEntry>>> entries;

  std::shared_ptr<PendingEntry> get(const PartialKey& key) const;
  void erase(const PartialKey& key);
  size_t size() const { return entries.size(); }
};

class PendingRegistry {
 public:
  explicit PendingRegistry(int64_t max_bytes = MAX_PENDING_BYTES, size_t max_pending = MAX_PENDING)
      : max_bytes_(max_bytes), max_pending_(max_pending) {}

  int64_t pending_bytes() const { return held_bytes_; }
  size_t records() const { return held_.size(); }

  std::pair<std::optional<Bytes>, size_t> assemble(const std::shared_ptr<Partial>& partial, const Message& msg,
                                                   size_t chunk = CHUNK);
  void release_all(Partial& partial);

 private:
  struct Record {
    std::shared_ptr<Partial> home;
    PartialKey key;
    std::shared_ptr<PendingEntry> entry;
    int64_t cost = 0;
  };
  void charge(const std::shared_ptr<Partial>& home, const PartialKey& key, const std::shared_ptr<PendingEntry>& entry,
              int64_t cost);
  void release(const std::shared_ptr<PendingEntry>& entry);

  int64_t max_bytes_;
  size_t max_pending_;
  std::map<uint64_t, Record> held_;
  int64_t held_bytes_ = 0;
  uint64_t next_serial_ = 1;
};

}
}
