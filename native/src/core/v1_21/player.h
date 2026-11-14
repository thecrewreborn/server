#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/accounts.h"
#include "core/files.h"
#include "core/json.h"
#include "core/pyobj.h"
#include "core/util.h"
#include "core/v1_21/module_state.h"

namespace tcr {
namespace v1_21 {

using obj::V;

constexpr size_t PERKS = 30;
constexpr int64_t PERK_MAX = 10;
constexpr size_t STAT_COUNT = 438;
constexpr size_t RECORD_FIELDS = 15;
constexpr size_t MAX_RECORDS = 512;
constexpr int64_t U8 = 0xFF, U16 = 0xFFFF, U32 = 0xFFFFFFFF;
constexpr uint64_t NO_ID = 0xFFFFFFFFFFFFFFFFull;
constexpr uint64_t PROLOGUE_DONE = 0x20000000078439C6ull;
constexpr const char kExtraFieldsKey[] = "extra_fields";
extern const char* const STATE_KEYS[];
extern const size_t STATE_KEY_COUNT;
extern const uint64_t ALL_ABILITIES[2];

using FieldBytes = std::map<int, Bytes>;

FieldBytes dlc_fields(const std::string& owned);
FieldBytes unlock_fields(const std::string& names);

bool set_bit(obj::Py& py, V& words, uint64_t n);

struct Record {
  V a, b, fields;
};

struct Player {
  std::string path;
  std::vector<V> stats = std::vector<V>(STAT_COUNT, obj::I(0));
  std::vector<Record> records;
  V zone = obj::I(0);
  V ids = obj::list_of_u64({NO_ID, NO_ID});
  V position = obj::list_of({0, 0, 0});
  V direction = obj::list_of({0, 0, 0});
  Bytes save_blob;
  V abilities = V::array();
  FieldBytes unlocks;
  FieldBytes dlc = dlc_fields("all");
  V progress = V::array();
  V bucks = obj::I(0);
  int64_t level = 1;
  V xp = obj::I(0);
  int64_t crew_credits = 0;
  int64_t cc_fixed = 0;
  int64_t perk_points = 0;
  V perks = obj::list_of(std::vector<int64_t>(PERKS, 0));
  int64_t field12 = 0;
  V skill_last = obj::I(0);
  std::vector<std::pair<std::string, V>> extra_fields;
  V missions_done = V::array();
  V activities_done = V::array();
  V garage = V::array();
  V garage_all = obj::B(false);
  V free_roam = obj::B(false);
  V rewarded = V::array();
  V table_bits = V::array();
  V owned_items = V::array();
  V summit = V::object();
  V group = obj::I(0);
  V group_joined = obj::I(0);
  V daily_last = obj::I(0);
  V save_ids = obj::list_of_u64(std::vector<uint64_t>(6, NO_ID));
  V update_charge = obj::I(0);
  V chase = obj::list_of_u64({NO_ID, 0xFF});
  V heat = obj::F(0.0);
  V collectibles = V::array();
  V map_state = V::array();

  Files* files = nullptr;
  std::optional<int64_t> max_bucks;
  std::vector<Earned> earned;

  static std::optional<Player> load(Files& files, const std::string& path, std::optional<int64_t> max_bucks,
                                    Raised* raised);
  std::optional<Raised> save();
  std::string save_text() const;
  std::optional<Raised> clamp();

  bool prologue_done(obj::Py& py) const;
  void complete_mission(obj::Py& py, uint64_t mission);
  void complete_activity(obj::Py& py, uint64_t activity);
  int64_t cc() const;
  bool spend_cc(int64_t amount);
  void unlock_free_roam();
  void set_progress_bit(obj::Py& py, uint64_t id) { set_bit(py, progress, id); }
  void set_stat(obj::Py& py, uint64_t stat_id, int64_t value);
  void set_record_field(obj::Py& py, uint64_t a, uint64_t b, uint64_t index, int64_t value);
  const V* extra_field(int64_t k) const;
};

int64_t fit(int64_t x, int64_t hi);

inline bool truthy(const V& v) { return obj::truthy(v); }

}
}
