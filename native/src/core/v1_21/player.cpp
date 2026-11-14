#include "core/v1_21/player.h"

#include <algorithm>
#include <cmath>

#include "core/v1_21/gamedata.h"

namespace tcr {
namespace v1_21 {

using obj::I;
using obj::Py;
using obj::U;

const char* const STATE_KEYS[] = {
    "zone", "ids", "position", "direction", "abilities", "progress", "bucks", "map_state", "skill_last",
    kExtraFieldsKey, "missions_done", "activities_done", "level", "xp", "crew_credits", "perk_points", "field12",
    "garage", "garage_all", "free_roam", "perks", "rewarded", "table_bits", "owned_items", "summit", "group",
    "group_joined", "daily_last", "save_ids", "update_charge", "chase", "heat", "collectibles"};
const size_t STATE_KEY_COUNT = sizeof(STATE_KEYS) / sizeof(STATE_KEYS[0]);
const uint64_t ALL_ABILITIES[2] = {0xFFFFFFFFFFFFFFFFull, 0x0FFBA3};

namespace {

const uint8_t kFogRevealed = 0xFF;
constexpr size_t kFogSize = 0x690;

Raised raise(const char* type, std::string text) { return Raised{type, std::move(text)}; }

bool ends_with_fields(const std::string& k) { return k.size() >= 7 && k.compare(k.size() - 7, 7, "_fields") == 0; }

int64_t fit_value(Py& py, const V& x, int64_t hi) {
  V i = py.to_int(x);
  if (py.failed()) return 0;
  if (py.lt(i, I(0))) return 0;
  if (py.gt(i, I(hi))) return hi;
  return obj::small_int(i).value_or(0);
}

bool any_out_of_range(Py& py, const std::vector<V>& xs, int64_t hi) {
  for (const V& x : xs) {
    bool low = py.le(I(0), x);
    if (py.failed()) return false;
    if (!low) return true;
    bool high = py.le(x, I(hi));
    if (py.failed()) return false;
    if (!high) return true;
  }
  return false;
}

std::optional<Raised> clamp_values(Player& p, const V* cc, const V* xp, const V* level, const V* points,
                                   const V* field12) {
  Py py;
  int64_t hi = p.max_bucks ? std::min(*p.max_bucks, U32) : U32;
  p.bucks = I(fit_value(py, p.bucks, hi));
  p.crew_credits = cc ? fit_value(py, *cc, U32) : fit(p.crew_credits, U32);
  p.xp = I(fit_value(py, xp ? *xp : p.xp, U32));
  p.level = level ? fit_value(py, *level, U8) : fit(p.level, U8);
  p.perk_points = points ? fit_value(py, *points, U16) : fit(p.perk_points, U16);
  p.field12 = field12 ? fit_value(py, *field12, U32) : fit(p.field12, U32);
  if (py.failed()) return py.raised();
  std::vector<V> perks = py.iter(p.perks);
  if (any_out_of_range(py, perks, PERK_MAX)) {
    V fitted = V::array();
    for (const V& x : perks) fitted.push(I(fit_value(py, x, PERK_MAX)));
    if (py.failed()) return py.raised();
    p.perks = std::move(fitted);
  }
  if (py.failed()) return py.raised();
  V& s = p.summit;
  const V tokens_key = V::string("tokens");
  if (py.contains(s, tokens_key)) {
    std::vector<V> tokens = py.iter(py.getitem(s, tokens_key));
    if (any_out_of_range(py, tokens, U8)) {
      V fitted = V::array();
      for (const V& x : tokens) fitted.push(I(fit_value(py, x, U8)));
      py.setitem(s, tokens_key, std::move(fitted));
    }
  }
  for (const char* k : {"threshold", "meter", "last_kind", "next_kind"}) {
    const V key = V::string(k);
    if (py.contains(s, key)) {
      int64_t f = fit_value(py, py.getitem(s, key), U8);
      py.setitem(s, key, I(f));
    }
  }
  if (py.failed()) return py.raised();
  return std::nullopt;
}

}

int64_t fit(int64_t x, int64_t hi) { return std::min(std::max(x, int64_t(0)), hi); }

FieldBytes dlc_fields(const std::string& owned) {
  if (py::strip(owned) == "none") return {};
  FieldBytes out;
  for (const auto& f : gamedata::dlc_fields()) out[f.first] = f.second;
  return out;
}

FieldBytes unlock_fields(const std::string& names) {
  (void)names;
  return {};
}

bool set_bit(Py& py, V& words, uint64_t n) {
  if (py.failed()) return false;
  const V word = U(n / 64);
  const V bit = U(n % 64);
  size_t len = py.len(words);
  if (py.failed()) return false;
  if (n / 64 >= len) {
    if (!words.is_array()) {
      return py.raise("AttributeError", std::string("'") + obj::type_name(words) + "' object has no attribute 'extend'");
    }
    while (words.items().size() <= n / 64) words.push(I(0));
  }
  V x = py.getitem(words, word);
  V set = py.band(py.rshift(x, bit), I(1));
  if (py.failed()) return false;
  if (obj::truthy(set)) return false;
  V* slot = py.item_ref(words, word);
  if (!slot) return false;
  V grown = py.bor(*slot, py.lshift(I(1), bit), true);
  if (py.failed()) return false;
  py.setitem(words, word, std::move(grown));
  return !py.failed();
}

std::optional<Player> Player::load(Files& files, const std::string& path, std::optional<int64_t> max_bucks,
                                   Raised* raised) {
  Player p;
  p.path = path;
  p.files = &files;
  p.max_bucks = max_bucks;
  if (path.empty() || !files.exists(path)) return p;
  std::string text;
  OsError err;
  if (!files.read(path, &text, &err)) {
    *raised = raise(err.code == 2 ? "FileNotFoundError" : "OSError", err.text());
    return std::nullopt;
  }
  std::string decoded;
  if (std::optional<std::string> bad = decode_text_file(&files, text, true, &decoded)) {
    *raised = raise("UnicodeDecodeError", *bad);
    return std::nullopt;
  }
  json::Error error;
  std::optional<V> parsed = json::loads(py::universal_newlines(decoded), json::kFileMaxDepth, nullptr, &error);
  if (!parsed) {
    *raised = Raised{error.type, error.text};
    return std::nullopt;
  }
  const V& data = *parsed;
  Py py;
  auto failed = [&]() {
    *raised = py.raised();
    return std::nullopt;
  };
  V stats = py.getitem(data, V::string("stats"));
  if (py.failed()) return failed();
  if (stats.is_array()) {
    p.stats.assign(stats.items().begin(), stats.items().begin() + std::min(stats.items().size(), STAT_COUNT));
    while (p.stats.size() < STAT_COUNT) p.stats.push_back(I(0));
  } else if (stats.is_string()) {
    *raised = raise("TypeError", "can only concatenate str (not \"list\") to str");
    return std::nullopt;
  } else if (stats.is_object()) {
    *raised = raise("KeyError", "slice(None, 438, None)");
    return std::nullopt;
  } else {
    *raised = raise("TypeError", std::string("'") + obj::type_name(stats) + "' object is not subscriptable");
    return std::nullopt;
  }
  V records = py.getitem(data, V::string("records"));
  std::vector<V> entries = py.iter(records);
  if (py.failed()) return failed();
  p.records.clear();
  for (const V& r : entries) {
    V a = py.getitem(r, V::string("a"));
    V b = py.getitem(r, V::string("b"));
    V fields = py.getitem(r, V::string("fields"));
    if (py.failed()) return failed();
    for (const V* k : {&a, &b}) {
      if (!obj::hashable(*k)) {
        *raised = raise("TypeError", std::string("unhashable type: '") + obj::type_name(*k) + "'");
        return std::nullopt;
      }
    }
    bool found = false;
    for (Record& e : p.records) {
      if (obj::same(e.a, a) && obj::same(e.b, b)) {
        e.fields = fields;
        found = true;
        break;
      }
    }
    if (!found) p.records.push_back(Record{a, b, fields});
  }
  const V *cc = nullptr, *xp = nullptr, *level = nullptr, *points = nullptr, *field12 = nullptr;
  const V* extra = nullptr;
  for (size_t k = 0; k < STATE_KEY_COUNT; k++) {
    const std::string key = STATE_KEYS[k];
    const V* v = data.get(key);
    if (!v) continue;
    if (key == "zone") p.zone = *v;
    else if (key == "ids") p.ids = *v;
    else if (key == "position") p.position = *v;
    else if (key == "direction") p.direction = *v;
    else if (key == "abilities") p.abilities = *v;
    else if (key == "progress") p.progress = *v;
    else if (key == "bucks") p.bucks = *v;
    else if (key == "map_state") p.map_state = *v;
    else if (key == "skill_last") p.skill_last = *v;
    else if (key == kExtraFieldsKey) extra = v;
    else if (key == "missions_done") p.missions_done = *v;
    else if (key == "activities_done") p.activities_done = *v;
    else if (key == "level") level = v;
    else if (key == "xp") xp = v;
    else if (key == "crew_credits") cc = v;
    else if (key == "perk_points") points = v;
    else if (key == "field12") field12 = v;
    else if (key == "garage") p.garage = *v;
    else if (key == "garage_all") p.garage_all = *v;
    else if (key == "free_roam") p.free_roam = *v;
    else if (key == "perks") p.perks = *v;
    else if (key == "rewarded") p.rewarded = *v;
    else if (key == "table_bits") p.table_bits = *v;
    else if (key == "owned_items") p.owned_items = *v;
    else if (key == "summit") p.summit = *v;
    else if (key == "group") p.group = *v;
    else if (key == "group_joined") p.group_joined = *v;
    else if (key == "daily_last") p.daily_last = *v;
    else if (key == "save_ids") p.save_ids = *v;
    else if (key == "update_charge") p.update_charge = *v;
    else if (key == "chase") p.chase = *v;
    else if (key == "heat") p.heat = *v;
    else if (key == "collectibles") p.collectibles = *v;
  }
  if (data.contains("level2") && !data.contains("perk_points")) points = data.get("level2");
  if (const V* blob = data.get("save_blob")) {
    std::optional<Bytes> b = py.fromhex(*blob);
    if (!b) return failed();
    p.save_blob = std::move(*b);
  }
  V table = extra ? *extra : V::object();
  for (const json::Member& m : data.members()) {
    if (!ends_with_fields(m.key) || m.key == kExtraFieldsKey || !m.value.is_object()) continue;
    for (const json::Member& f : m.value.members()) {
      if (!table.is_object()) {
        *raised = raise("AttributeError", std::string("'") + obj::type_name(table) +
                                              "' object has no attribute 'setdefault'");
        return std::nullopt;
      }
      if (!table.contains(f.key)) table.set(f.key, f.value);
    }
  }
  if (!table.is_object()) {
    *raised = raise("AttributeError", std::string("'") + obj::type_name(table) + "' object has no attribute 'items'");
    return std::nullopt;
  }
  p.extra_fields.clear();
  for (const json::Member& m : table.members()) {
    std::string err;
    std::optional<std::string> key = py::parse_int(m.key, &err);
    if (!key) {
      *raised = raise("ValueError", err);
      return std::nullopt;
    }
    bool found = false;
    for (auto& have : p.extra_fields) {
      if (have.first == *key) {
        have.second = m.value;
        found = true;
        break;
      }
    }
    if (!found) p.extra_fields.emplace_back(*key, m.value);
  }
  if (std::optional<Raised> r = clamp_values(p, cc, xp, level, points, field12)) {
    *raised = *r;
    return std::nullopt;
  }
  return p;
}

std::optional<Raised> Player::clamp() { return clamp_values(*this, nullptr, nullptr, nullptr, nullptr, nullptr); }

std::string Player::save_text() const {
  V d = V::object();
  V st = V::array();
  for (const V& x : stats) st.push(x);
  d.set("stats", std::move(st));
  V recs = V::array();
  for (const Record& r : records) {
    V e = V::object();
    e.set("a", r.a);
    e.set("b", r.b);
    e.set("fields", r.fields);
    recs.push(std::move(e));
  }
  d.set("records", std::move(recs));
  V extra = V::object();
  for (const auto& e : extra_fields) {
    json::Member m;
    m.key = e.first;
    m.value = e.second;
    m.int_key = true;
    extra.members().push_back(std::move(m));
  }
  const std::pair<const char*, const V*> values[] = {
      {"zone", &zone},
      {"ids", &ids},
      {"position", &position},
      {"direction", &direction},
      {"abilities", &abilities},
      {"progress", &progress},
      {"bucks", &bucks},
      {"map_state", &map_state},
      {"skill_last", &skill_last},
      {kExtraFieldsKey, &extra},
      {"missions_done", &missions_done},
      {"activities_done", &activities_done},
      {"level", nullptr},
      {"xp", &xp},
      {"crew_credits", nullptr},
      {"perk_points", nullptr},
      {"field12", nullptr},
      {"garage", &garage},
      {"garage_all", &garage_all},
      {"free_roam", &free_roam},
      {"perks", &perks},
      {"rewarded", &rewarded},
      {"table_bits", &table_bits},
      {"owned_items", &owned_items},
      {"summit", &summit},
      {"group", &group},
      {"group_joined", &group_joined},
      {"daily_last", &daily_last},
      {"save_ids", &save_ids},
      {"update_charge", &update_charge},
      {"chase", &chase},
      {"heat", &heat},
      {"collectibles", &collectibles},
  };
  for (const auto& kv : values) {
    std::string key = kv.first;
    if (kv.second) {
      d.set(key, *kv.second);
    } else {
      int64_t i = key == "level" ? level : key == "crew_credits" ? crew_credits
                                                : key == "perk_points" ? perk_points : field12;
      d.set(key, I(i));
    }
  }
  d.set("save_blob", V::string(py::hex(save_blob)));
  return json::dumps(d);
}

std::optional<Raised> Player::save() {
  if (path.empty() || !files) return std::nullopt;
  if (std::optional<Raised> r = clamp()) return r;
  FileStat st;
  OsError err;
  if (!files->write_atomic(path, save_text(), false, -1, -1, &st, &err)) {
    return raise(err.code == 2 ? "FileNotFoundError" : "OSError", err.text());
  }
  return std::nullopt;
}

bool Player::prologue_done(Py& py) const { return py.contains(missions_done, U(PROLOGUE_DONE)); }

namespace {

void append_new(Py& py, V& xs, uint64_t x) {
  const V item = U(x);
  bool in = py.contains(xs, item);
  if (py.failed() || in) return;
  if (!xs.is_array()) {
    py.raise("AttributeError", std::string("'") + obj::type_name(xs) + "' object has no attribute 'append'");
    return;
  }
  xs.push(item);
}

}

void Player::complete_mission(Py& py, uint64_t mission) { append_new(py, missions_done, mission); }

void Player::complete_activity(Py& py, uint64_t activity) { append_new(py, activities_done, activity); }

int64_t Player::cc() const { return fit(cc_fixed ? cc_fixed : crew_credits, U32); }

bool Player::spend_cc(int64_t amount) {
  if (cc() < amount) return false;
  if (!cc_fixed) crew_credits -= amount;
  return true;
}

void Player::unlock_free_roam() {
  free_roam = obj::B(true);
  abilities = obj::list_of_u64({ALL_ABILITIES[0], ALL_ABILITIES[1]});
  save_blob.assign(kFogSize, kFogRevealed);
}

void Player::set_stat(Py& py, uint64_t stat_id, int64_t value) {
  (void)py;
  if (stat_id >= STAT_COUNT) return;
  if (stat_id >= stats.size()) stats.resize(stat_id + 1, I(0));
  stats[stat_id] = I(value);
}

void Player::set_record_field(Py& py, uint64_t a, uint64_t b, uint64_t index, int64_t value) {
  if (py.failed()) return;
  const V ka = U(a), kb = U(b);
  Record* rec = nullptr;
  for (Record& r : records) {
    if (obj::same(r.a, ka) && obj::same(r.b, kb)) {
      rec = &r;
      break;
    }
  }
  if (!rec) {
    if (records.size() >= MAX_RECORDS) return;
    records.push_back(Record{ka, kb, obj::list_of(std::vector<int64_t>(RECORD_FIELDS, 0))});
    rec = &records.back();
  }
  if (index < RECORD_FIELDS) py.setitem(rec->fields, U(index), I(value));
}

const V* Player::extra_field(int64_t k) const {
  const std::string key = py::int_to_text(k);
  for (const auto& e : extra_fields) {
    if (e.first == key) return &e.second;
  }
  return nullptr;
}

}
}
