#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/accounts.h"
#include "core/util.h"
#include "core/v1_21/player.h"

namespace tcr {
class Core;
namespace v1_21 {
namespace cars {

constexpr uint64_t NONE = 0xFFFFFFFFFFFFFFFFull;
constexpr size_t OWNED_WORDS = 0x8C;
constexpr size_t OWNED_SLOTS = 11;
constexpr size_t SLOT_COUNT = 38;
constexpr size_t PLAYER_PARTS = 250;
constexpr uint8_t NEW = 4, STORED = 1;
constexpr uint8_t BOUGHT = 3;
constexpr double CC_RATE = 4.50045;
constexpr int64_t NO_PART = 0xFFFF;

using Garage = std::vector<std::pair<V, std::vector<Bytes>>>;

struct Variant {
  uint64_t model = 0;
  uint64_t spec = 0;
  uint64_t default_part = 0;
  int64_t tier = 0;
  std::vector<int64_t> slots;
};

struct Catalog {
  std::unordered_map<uint64_t, Variant> variants;
  std::unordered_map<uint64_t, int64_t> parts;
  std::unordered_map<uint64_t, int64_t> prices;
  int64_t budget = 0;
  uint64_t dealer = 0;
  std::vector<int64_t> slot_defaults;
  std::map<uint64_t, std::vector<int64_t>> spec_prices;
  std::vector<uint64_t> special_specs;

  int64_t part(uint64_t pid) const;
  const Variant* variant(uint64_t id) const;
  std::optional<int64_t> price(uint64_t model) const;
  bool special(uint64_t spec) const;
};

const Catalog* catalog(const Core& core);

const std::vector<size_t>& list_slots();
const Bytes& empty_config();

Bytes array(uint32_t count, const Bytes& items);
std::vector<int64_t> slots(const Catalog& c, const Variant& v, uint64_t a, uint64_t b, uint64_t d);
std::optional<Bytes> config(const Catalog& c, uint64_t variant, const std::vector<int64_t>& s, int64_t state,
                            Raised* raised);
std::optional<Bytes> part_list(const std::vector<int64_t>& s, Raised* raised);
std::optional<Bytes> garage_field(const Garage& entries, Raised* raised);
std::optional<Garage> parse_garage(const Bytes& field, Raised* raised);
std::optional<uint64_t> spec(const Bytes& cfg, Raised* raised);
std::optional<uint64_t> variant_of(const Bytes& cfg, Raised* raised);
Bytes with_state(const Bytes& cfg, uint8_t state);
std::optional<Bytes> player_parts(const Garage& entries, const std::vector<Bytes>& fields, Raised* raised);
std::optional<Garage> entries(const Player& p, Raised* raised);
std::vector<Bytes>* configs_of(Garage& garage, const V& model);
void store(Player& p, const Garage& garage);
bool spec_price(const Catalog& c, const Garage& garage, const Variant& v, std::optional<int64_t>* price,
                Raised* raised);

}
}
}
