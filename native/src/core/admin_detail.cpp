#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/admin.h"
#include "core/files.h"
#include "core/pyobj.h"
#include "core/util.h"
#include "core/v1_21/cars.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/player.h"
#include "core/v1_21/rewards.h"
#include "core/v1_21/scores.h"

namespace tcr::admin {

namespace {

using json::Value;
namespace gd = v1_21::gamedata;

constexpr int64_t RACER_CHUNKS = 10;
constexpr int64_t POLICE_CHUNKS = 15;

struct MissionRow {
  uint64_t id = 0;
  int64_t kind = 0;
  std::optional<std::string> name;
  std::vector<int64_t> thresholds;
  bool lower = false;
};

struct ActivityRow {
  uint64_t id = 0;
  uint64_t region = 0;
  std::optional<std::string> name, district;
  std::vector<int64_t> thresholds;
  bool lower = false;
};

struct Tables {
  std::vector<MissionRow> missions;
  std::map<int64_t, std::optional<std::string>> kind_labels;
  std::vector<std::pair<uint64_t, std::vector<ActivityRow>>> regions;
  size_t activity_count = 0;
  std::unordered_map<uint64_t, std::string> region_names;
  std::unordered_map<uint64_t, std::string> car_names;
  std::unordered_map<uint64_t, uint64_t> variant_model;
  std::vector<std::pair<uint64_t, int64_t>> sets;
  std::vector<uint64_t> landmark_bits;
  std::vector<uint64_t> landmark_regions;
  std::vector<std::optional<std::string>> summit_names;
  int64_t max_level = 0;
};

std::optional<std::string> text_of(const Value* v) {
  if (v && v->is_string()) return v->as_string();
  return std::nullopt;
}

std::optional<uint64_t> hex_text(const std::string& s) {
  if (s.size() < 3 || s.size() > 18 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return std::nullopt;
  for (size_t i = 2; i < s.size(); i++) {
    if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return std::nullopt;
  }
  return gd::hex_id(s);
}

std::optional<uint64_t> hex_value(const Value* v) {
  if (!v || !v->is_string()) return std::nullopt;
  return hex_text(v->as_string());
}

std::vector<int64_t> int_list(const Value* v) {
  std::vector<int64_t> out;
  if (!v || !v->is_array()) return out;
  for (const Value& x : v->items()) {
    std::optional<int64_t> n = x.is_int() ? x.as_i64() : std::nullopt;
    out.push_back(n.value_or(0));
  }
  return out;
}

bool natural_less(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    bool da = std::isdigit(static_cast<unsigned char>(a[i])), db = std::isdigit(static_cast<unsigned char>(b[j]));
    if (da && db) {
      size_t ei = i, ej = j;
      while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ei++;
      while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ej++;
      std::string na = a.substr(i, ei - i), nb = b.substr(j, ej - j);
      na.erase(0, std::min(na.find_first_not_of('0'), na.size()));
      nb.erase(0, std::min(nb.find_first_not_of('0'), nb.size()));
      if (na.size() != nb.size()) return na.size() < nb.size();
      if (na != nb) return na < nb;
      i = ei;
      j = ej;
    } else {
      if (a[i] != b[j]) return a[i] < b[j];
      i++;
      j++;
    }
  }
  return a.size() - i < b.size() - j;
}

Tables load() {
  Tables t;
  const Value* rewards = gd::table("rewards.json");
  const Value* names = gd::table("names.json");
  auto obj = [](const Value* table, const char* key) -> const Value* {
    const Value* v = table && table->is_object() ? table->get(key) : nullptr;
    return v && v->is_object() ? v : nullptr;
  };
  std::set<uint64_t> lower;
  if (const Value* lb = rewards && rewards->is_object() ? rewards->get("lower_better") : nullptr) {
    if (lb->is_array()) {
      for (const Value& x : lb->items()) {
        if (std::optional<uint64_t> id = hex_value(&x)) lower.insert(*id);
      }
    }
  }
  if (rewards && rewards->is_object()) {
    std::vector<int64_t> table = int_list(rewards->get("table"));
    t.max_level = table.size() > v1_21::rewards::MAX_LEVEL ? table[v1_21::rewards::MAX_LEVEL] : 0;
  }
  const Value* mission_names = obj(names, "missions");
  if (const Value* missions = obj(rewards, "missions")) {
    for (const json::Member& m : missions->members()) {
      std::optional<uint64_t> id = hex_text(m.key);
      if (!id || !m.value.is_object()) continue;
      MissionRow row;
      row.id = *id;
      const Value* online = m.value.get("online");
      row.kind = online && online->is_int() ? online->as_i64().value_or(0) : 0;
      row.thresholds = int_list(m.value.get("thresholds"));
      row.lower = lower.count(*id) > 0;
      const Value* n = mission_names ? mission_names->get(m.key) : nullptr;
      row.name = n && n->is_object() ? text_of(n->get("name")) : std::nullopt;
      t.missions.push_back(std::move(row));
    }
  }
  if (const Value* kinds = obj(names, "mission_kinds")) {
    for (const json::Member& m : kinds->members()) {
      std::optional<int64_t> k = py::int_text_to_i64(m.key);
      if (k) t.kind_labels[*k] = text_of(&m.value);
    }
  }
  if (const Value* regions = obj(names, "regions")) {
    for (const json::Member& m : regions->members()) {
      std::optional<uint64_t> id = hex_text(m.key);
      if (id && m.value.is_string()) t.region_names[*id] = m.value.as_string();
    }
  }
  const Value* region_table = gd::table("regions.json");
  if (region_table && region_table->is_object()) {
    for (const char* key : {"regions", "bits"}) {
      const Value* list = region_table->get(key);
      if (!list || !list->is_array()) continue;
      for (const Value& x : list->items()) {
        uint64_t id = hex_value(&x).value_or(0);
        (std::strcmp(key, "regions") == 0 ? t.landmark_regions : t.landmark_bits).push_back(id);
      }
    }
  }
  for (uint64_t r : t.landmark_regions) t.regions.emplace_back(r, std::vector<ActivityRow>());
  const Value* activity_names = obj(names, "activities");
  if (const Value* activities = obj(rewards, "activities")) {
    for (const json::Member& m : activities->members()) {
      std::optional<uint64_t> id = hex_text(m.key);
      if (!id || !m.value.is_object()) continue;
      ActivityRow row;
      row.id = *id;
      row.region = hex_value(m.value.get("region")).value_or(0);
      row.thresholds = int_list(m.value.get("thresholds"));
      row.lower = lower.count(*id) > 0;
      const Value* n = activity_names ? activity_names->get(m.key) : nullptr;
      if (n && n->is_object()) {
        row.name = text_of(n->get("name"));
        row.district = text_of(n->get("district"));
      }
      auto at = std::find_if(t.regions.begin(), t.regions.end(), [&](const auto& r) { return r.first == row.region; });
      if (at == t.regions.end()) at = t.regions.emplace(t.regions.end(), row.region, std::vector<ActivityRow>());
      at->second.push_back(std::move(row));
      t.activity_count++;
    }
  }
  for (auto& r : t.regions) {
    std::stable_sort(r.second.begin(), r.second.end(), [](const ActivityRow& a, const ActivityRow& b) {
      const std::string da = a.district.value_or(""), db = b.district.value_or("");
      if (da != db) return natural_less(da, db);
      return natural_less(a.name.value_or(""), b.name.value_or(""));
    });
  }
  if (const Value* cars = obj(names, "cars")) {
    for (const json::Member& m : cars->members()) {
      std::optional<uint64_t> id = hex_text(m.key);
      if (id && m.value.is_string()) t.car_names[*id] = m.value.as_string();
    }
  }
  if (const Value* variants = obj(gd::table("cars.json"), "variants")) {
    for (const json::Member& m : variants->members()) {
      std::optional<uint64_t> id = hex_text(m.key);
      std::optional<uint64_t> model = m.value.is_object() ? hex_value(m.value.get("model")) : std::nullopt;
      if (id && model) t.variant_model[*id] = *model;
    }
  }
  const Value* collectibles = gd::table("collectibles.json");
  if (const Value* needed = obj(collectibles, "needed")) {
    for (const json::Member& m : needed->members()) {
      std::optional<uint64_t> id = hex_text(m.key);
      if (id) t.sets.emplace_back(*id, m.value.is_int() ? m.value.as_i64().value_or(0) : 0);
    }
  }
  const Value* summit = gd::table("summit.json");
  if (const Value* records = summit && summit->is_object() ? summit->get("records") : nullptr) {
    if (records->is_array()) {
      for (const Value& r : records->items()) {
        t.summit_names.push_back(r.is_object() ? text_of(r.get("name")) : std::nullopt);
      }
    }
  }
  return t;
}

const Tables& tables() {
  static const Tables t = load();
  return t;
}

using SetIndex = std::unordered_map<uint64_t, uint64_t>;
const SetIndex& set_index() {
  static const SetIndex index = [] {
    SetIndex out;
    const Value* table = gd::table("collectibles.json");
    const Value* set_of = table && table->is_object() ? table->get("set_of") : nullptr;
    if (set_of && set_of->is_object()) {
      for (const json::Member& m : set_of->members()) {
        std::optional<uint64_t> id = hex_text(m.key);
        std::optional<uint64_t> set = hex_value(&m.value);
        if (id && set) out[*id] = *set;
      }
    }
    return out;
  }();
  return index;
}

Value hex(uint64_t id) { return Value::string(gd::hex_key(id)); }

Value text_or_null(const std::optional<std::string>& s) { return s ? Value::string(*s) : Value(); }

std::optional<uint64_t> id_of(const Value& v) { return v.is_int() ? v.as_u64() : std::nullopt; }

std::unordered_set<uint64_t> id_set(const Value* list, std::vector<uint64_t>* order = nullptr) {
  std::unordered_set<uint64_t> out;
  if (!list || !list->is_array()) return out;
  for (const Value& v : list->items()) {
    std::optional<uint64_t> id = id_of(v);
    if (id && out.insert(*id).second && order) order->push_back(*id);
  }
  return out;
}

Value medal_of(const std::vector<int64_t>& th, bool lower, int64_t score, int64_t level, int64_t max_level) {
  if (lower || th.size() < 4) return Value();
  if (th[0] == 0 && th[1] == 0 && th[2] == 0 && th[3] == 0) return Value();
  for (int64_t i : {3, 2, 1, 0}) {
    if (th[static_cast<size_t>(i)] <= score) return Value::integer(i == 3 && level < max_level ? 2 : i);
  }
  return Value();
}

void medal_fields(Value* item, uint64_t id, const std::vector<int64_t>& th, bool lower, const DetailContext& ctx,
                  const std::unordered_map<uint64_t, int64_t>& rewarded, int64_t level, int64_t max_level) {
  auto best = ctx.best.find(id);
  if (best != ctx.best.end()) {
    item->set("medal", medal_of(th, lower, best->second, level, max_level));
    item->set("medal_source", Value::string("result"));
    item->set("score", Value::integer(best->second));
    return;
  }
  auto r = rewarded.find(id);
  item->set("medal", r != rewarded.end() ? Value::integer(r->second) : Value());
  item->set("medal_source", r != rewarded.end() ? Value::string("reward") : Value());
  item->set("score", Value());
}

Value other_ids(const std::vector<uint64_t>& done, const std::unordered_set<uint64_t>& known) {
  Value out = Value::array();
  for (uint64_t id : done) {
    if (!known.count(id)) out.push(hex(id));
  }
  return out;
}

Value missions(const Tables& t, const Value& save, const DetailContext& ctx,
               const std::unordered_map<uint64_t, int64_t>& rewarded, int64_t level) {
  std::vector<uint64_t> order;
  std::unordered_set<uint64_t> done = id_set(save.get("missions_done"), &order);
  std::map<int64_t, std::pair<Value, int64_t>> groups;
  std::unordered_set<uint64_t> known;
  int64_t done_count = 0;
  for (const MissionRow& m : t.missions) {
    known.insert(m.id);
    bool is_done = done.count(m.id) > 0;
    auto& g = groups.try_emplace(m.kind, Value::array(), 0).first->second;
    Value item = Value::object();
    item.set("id", hex(m.id));
    item.set("name", text_or_null(m.name));
    item.set("done", Value::boolean(is_done));
    medal_fields(&item, m.id, m.thresholds, m.lower, ctx, rewarded, level, t.max_level);
    g.first.push(std::move(item));
    g.second += is_done;
    done_count += is_done;
  }
  Value list = Value::array();
  for (auto& [kind, g] : groups) {
    Value group = Value::object();
    group.set("kind", Value::integer(kind));
    auto label = t.kind_labels.find(kind);
    group.set("label", label != t.kind_labels.end() ? text_or_null(label->second) : Value());
    group.set("done", Value::integer(g.second));
    group.set("total", Value::uinteger(g.first.size()));
    group.set("items", std::move(g.first));
    list.push(std::move(group));
  }
  Value out = Value::object();
  out.set("done", Value::integer(done_count));
  out.set("total", Value::uinteger(t.missions.size()));
  out.set("groups", std::move(list));
  out.set("other", other_ids(order, known));
  return out;
}

Value activities(const Tables& t, const Value& save, const DetailContext& ctx,
                 const std::unordered_map<uint64_t, int64_t>& rewarded, int64_t level) {
  std::vector<uint64_t> order;
  std::unordered_set<uint64_t> done = id_set(save.get("activities_done"), &order);
  std::unordered_set<uint64_t> known;
  int64_t done_count = 0;
  Value regions = Value::array();
  for (const auto& [region, rows] : t.regions) {
    Value items = Value::array();
    int64_t region_done = 0;
    for (const ActivityRow& a : rows) {
      known.insert(a.id);
      bool is_done = done.count(a.id) > 0;
      Value item = Value::object();
      item.set("id", hex(a.id));
      item.set("name", text_or_null(a.name));
      item.set("district", text_or_null(a.district));
      item.set("kind", Value::string("skill"));
      item.set("done", Value::boolean(is_done));
      medal_fields(&item, a.id, a.thresholds, a.lower, ctx, rewarded, level, t.max_level);
      items.push(std::move(item));
      region_done += is_done;
    }
    done_count += region_done;
    Value r = Value::object();
    r.set("region", hex(region));
    auto name = t.region_names.find(region);
    r.set("name", name != t.region_names.end() ? Value::string(name->second) : Value());
    r.set("done", Value::integer(region_done));
    r.set("total", Value::uinteger(rows.size()));
    r.set("items", std::move(items));
    regions.push(std::move(r));
  }
  Value out = Value::object();
  out.set("done", Value::integer(done_count));
  out.set("total", Value::uinteger(t.activity_count));
  out.set("regions", std::move(regions));
  out.set("other", other_ids(order, known));
  return out;
}

std::unordered_map<uint64_t, int64_t> reward_medals(const Value* v) {
  std::unordered_map<uint64_t, int64_t> out;
  if (!v || !v->is_object()) return out;
  for (const json::Member& m : v->members()) {
    std::optional<uint64_t> id = hex_text(m.key);
    std::optional<int64_t> medal = m.value.is_int() ? m.value.as_i64() : std::nullopt;
    if (id && medal && *medal >= 0 && *medal <= 3) out[*id] = *medal;
  }
  return out;
}

struct Car {
  std::optional<uint64_t> model;
  std::vector<Bytes> configs;
  bool pack = false;
};

std::vector<Car> from_field(const Bytes& field, bool pack) {
  std::vector<Car> out;
  Raised raised;
  std::optional<v1_21::cars::Garage> g = v1_21::cars::parse_garage(field, &raised);
  if (!g) return out;
  for (auto& [model, configs] : *g) out.push_back(Car{id_of(model), std::move(configs), pack});
  return out;
}

const Value* own_field_72(const Value& save) {
  const Value* own = nullptr;
  if (const Value* extra = save.get(v1_21::kExtraFieldsKey); extra && extra->is_object()) own = extra->get("72");
  for (const json::Member& m : save.members()) {
    if (own) break;
    if (m.key.size() > 7 && m.key.compare(m.key.size() - 7, 7, "_fields") == 0 && m.value.is_object()) {
      own = m.value.get("72");
    }
  }
  return own;
}

std::vector<Car> garage(const Value& save, const DetailContext& ctx, const std::unordered_set<uint64_t>& missions) {
  const Value* all = save.get("garage_all");
  const Value* own72 = own_field_72(save);
  if (own72 && !(all && obj::truthy(*all))) {
    std::optional<Bytes> field = own72->is_string() ? py::fromhex(own72->as_string()) : std::nullopt;
    return field ? from_field(*field, false) : std::vector<Car>();
  }
  std::vector<Car> own;
  if (const Value* g = save.get("garage"); g && g->is_array()) {
    for (const Value& e : g->items()) {
      if (!e.is_object()) continue;
      const Value* model = e.get("model");
      const Value* configs = e.get("configs");
      Car car{model ? id_of(*model) : std::nullopt, {}, false};
      if (configs && configs->is_array()) {
        for (const Value& c : configs->items()) {
          std::optional<Bytes> b = c.is_string() ? py::fromhex(c.as_string()) : std::nullopt;
          if (b) car.configs.push_back(std::move(*b));
        }
      }
      own.push_back(std::move(car));
    }
  }
  if (!ctx.pack_garage || !missions.count(v1_21::PROLOGUE_DONE)) return own;
  std::vector<Car> out;
  for (Car& p : from_field(*ctx.pack_garage, true)) {
    bool owned = std::any_of(own.begin(), own.end(), [&](const Car& o) { return o.model && o.model == p.model; });
    if (!owned) out.push_back(std::move(p));
  }
  for (Car& o : own) out.push_back(std::move(o));
  return out;
}

uint64_t u64_at(const Bytes& b, size_t at) {
  uint64_t v = 0;
  for (size_t i = 0; i < 8; i++) v = v << 8 | b[at + i];
  return v;
}

Value car_list(const Tables& t, const Value& save, const DetailContext& ctx,
               const std::unordered_set<uint64_t>& missions) {
  std::optional<uint64_t> cur_model, cur_spec;
  if (const Value* ids = save.get("ids"); ids && ids->is_array() && ids->size() >= 2) {
    cur_model = id_of(ids->items()[0]);
    cur_spec = id_of(ids->items()[1]);
  }
  struct Row {
    Value v;
    bool exact = false, model = false;
  };
  std::vector<Row> rows;
  for (const Car& car : garage(save, ctx, missions)) {
    for (const Bytes& cfg : car.configs) {
      Row row;
      row.v = Value::object();
      row.v.set("model", car.model ? hex(*car.model) : Value());
      auto name = car.model ? t.car_names.find(*car.model) : t.car_names.end();
      row.v.set("name", name != t.car_names.end() ? Value::string(name->second) : Value());
      std::optional<uint64_t> spec, variant;
      if (cfg.size() >= 16) {
        spec = u64_at(cfg, 0);
        variant = u64_at(cfg, 8);
      }
      row.v.set("variant", variant ? hex(*variant) : Value());
      row.v.set("spec", spec ? hex(*spec) : Value());
      row.v.set("source", Value::string(car.pack ? "pack" : "own"));
      row.model = car.model && cur_model && *car.model == *cur_model;
      row.exact = row.model && spec && cur_spec && *spec == *cur_spec;
      rows.push_back(std::move(row));
    }
  }
  auto cur = std::find_if(rows.begin(), rows.end(), [](const Row& r) { return r.exact; });
  if (cur == rows.end()) cur = std::find_if(rows.begin(), rows.end(), [](const Row& r) { return r.model; });
  Value out = Value::array();
  if (cur != rows.end()) {
    cur->v.set("current", Value::boolean(true));
    out.push(std::move(cur->v));
  }
  for (auto it = rows.begin(); it != rows.end(); ++it) {
    if (it == cur) continue;
    it->v.set("current", Value::boolean(false));
    out.push(std::move(it->v));
  }
  return out;
}

Value chase_car(const Tables& t, const Value* car, const Value* chunks, int64_t of) {
  std::optional<uint64_t> variant = car ? id_of(*car) : std::nullopt;
  if (!variant || *variant == v1_21::NO_ID) return Value();
  Value out = Value::object();
  out.set("variant", hex(*variant));
  auto model = t.variant_model.find(*variant);
  out.set("model", model != t.variant_model.end() ? hex(model->second) : Value());
  auto name = model != t.variant_model.end() ? t.car_names.find(model->second) : t.car_names.end();
  out.set("name", name != t.car_names.end() ? Value::string(name->second) : Value());
  out.set("chunks", Value::integer(chunks ? v1_21::chase_chunks(*chunks) : 0));
  out.set("of", Value::integer(of));
  return out;
}

std::unordered_map<uint64_t, int64_t> found_per_set(const SetIndex& set_of, const Value* list) {
  std::unordered_map<uint64_t, int64_t> have;
  std::unordered_set<uint64_t> seen;
  if (list && list->is_array()) {
    for (const Value& e : list->items()) {
      if (!e.is_array() || e.size() != 2) continue;
      std::optional<uint64_t> id = id_of(e.items()[0]), value = id_of(e.items()[1]);
      if (!id || !value || *value > 0xFFFFFFFFull || !seen.insert(*id).second) continue;
      uint32_t bits = static_cast<uint32_t>(*value);
      float f;
      std::memcpy(&f, &bits, sizeof(f));
      auto set = set_of.find(*id);
      if (set != set_of.end() && f >= 1.0f) have[set->second]++;
    }
  }
  return have;
}

Value collectibles(const Tables& t, const Value& save) {
  std::unordered_map<uint64_t, int64_t> have = found_per_set(set_index(), save.get("collectibles"));
  Value sets = Value::array();
  int64_t total = 0, found = 0;
  for (const auto& [set, of] : t.sets) {
    Value s = Value::object();
    s.set("set", hex(set));
    s.set("have", Value::integer(have[set]));
    s.set("of", Value::integer(of));
    sets.push(std::move(s));
    total += of;
    found += have[set];
  }
  Value out = Value::object();
  out.set("have", Value::integer(found));
  out.set("total", Value::integer(total));
  out.set("sets", std::move(sets));
  return out;
}

Value landmarks(const Tables& t, const Value& save) {
  std::vector<uint64_t> words;
  if (const Value* bits = save.get("table_bits"); bits && bits->is_array()) {
    for (const Value& w : bits->items()) words.push_back(id_of(w).value_or(0));
  }
  Value regions = Value::array();
  int64_t total = 0, found = 0;
  for (uint64_t region : t.landmark_regions) {
    int64_t have = 0, of = 0;
    for (size_t n = 0; n < t.landmark_bits.size(); n++) {
      if (t.landmark_bits[n] != region) continue;
      of++;
      if ((n >> 6) < words.size() && (words[n >> 6] >> (n & 63) & 1)) have++;
    }
    Value r = Value::object();
    r.set("region", hex(region));
    auto name = t.region_names.find(region);
    r.set("name", name != t.region_names.end() ? Value::string(name->second) : Value());
    r.set("have", Value::integer(have));
    r.set("of", Value::integer(of));
    regions.push(std::move(r));
    total += of;
    found += have;
  }
  Value out = Value::object();
  out.set("have", Value::integer(found));
  out.set("total", Value::integer(total));
  out.set("regions", std::move(regions));
  return out;
}

Value perk_view(const Value& save) {
  Value out = Value::object();
  const Value* points = save.get("perk_points");
  out.set("points", points && points->is_number() ? *points : Value());
  Value slots = Value::array();
  if (const Value* perks = save.get("perks"); perks && perks->is_array()) {
    int64_t slot = 0;
    for (const Value& p : perks->items()) {
      Value s = Value::object();
      s.set("slot", Value::integer(slot++));
      std::optional<int64_t> level = p.is_int() ? p.as_i64() : std::nullopt;
      s.set("level", level ? Value::integer(*level) : Value());
      slots.push(std::move(s));
    }
  }
  out.set("slots", std::move(slots));
  return out;
}

Value summit_view(const Tables& t, const Value& save) {
  const Value* state = save.get("summit");
  Value out = Value::object();
  std::optional<int64_t> index;
  if (state && state->is_object()) {
    const Value* i = state->get("index");
    index = i && i->is_int() ? i->as_i64() : std::nullopt;
  }
  bool known = index && *index >= 0 && static_cast<size_t>(*index) < t.summit_names.size();
  out.set("name", known ? text_or_null(t.summit_names[static_cast<size_t>(*index)]) : Value());
  out.set("state", state ? *state : Value());
  return out;
}

const char* const kShown[] = {"level", "xp", "bucks", "crew_credits", "perk_points", "ids", "perks", "group",
                              "group_joined", "zone", "position", "garage", "missions_done", "activities_done",
                              "skill_medals", "collectibles", "table_bits", "chase_racer_car", "chase_racer_chunks",
                              "chase_police_car", "chase_police_chunks", "summit"};

}

json::Value player_detail(const json::Value& save, const DetailContext& ctx) {
  const Tables& t = tables();
  const Value* lv = save.get("level");
  const int64_t level = lv && lv->is_int() ? lv->as_i64().value_or(0) : 0;
  const std::unordered_map<uint64_t, int64_t> rewarded = reward_medals(save.get("skill_medals"));
  const std::unordered_set<uint64_t> missions_done = id_set(save.get("missions_done"));
  Value out = Value::object();
  out.set("cars", car_list(t, save, ctx, missions_done));
  Value chase = Value::object();
  chase.set("racer", chase_car(t, save.get("chase_racer_car"), save.get("chase_racer_chunks"), RACER_CHUNKS));
  chase.set("police", chase_car(t, save.get("chase_police_car"), save.get("chase_police_chunks"), POLICE_CHUNKS));
  out.set("chase", std::move(chase));
  out.set("missions", missions(t, save, ctx, rewarded, level));
  out.set("activities", activities(t, save, ctx, rewarded, level));
  out.set("collectibles", collectibles(t, save));
  out.set("landmarks", landmarks(t, save));
  out.set("perks", perk_view(save));
  out.set("summit", summit_view(t, save));
  Value raw = Value::array();
  for (const json::Member& m : save.members()) {
    if (std::none_of(std::begin(kShown), std::end(kShown), [&](const char* k) { return m.key == k; })) {
      raw.push(Value::string(m.key));
    }
  }
  out.set("raw_keys", std::move(raw));
  return out;
}

int64_t collectibles_found(const json::Value* list) {
  int64_t n = 0;
  for (const auto& set : found_per_set(set_index(), list)) n += set.second;
  return n;
}

std::map<uint64_t, int64_t> best_scores(Files* files, const std::string& data, uint64_t player) {
  std::map<uint64_t, int64_t> out;
  std::string decoded;
  if (decode_text_file(files, data, true, &decoded)) return out;
  std::optional<Value> store = json::loads(py::universal_newlines(decoded), json::kFileMaxDepth);
  if (!store || !store->is_object()) return out;
  const Tables& t = tables();
  std::unordered_set<uint64_t> lower;
  for (const MissionRow& m : t.missions) {
    if (m.lower) lower.insert(m.id);
  }
  for (const auto& region : t.regions) {
    for (const ActivityRow& a : region.second) {
      if (a.lower) lower.insert(a.id);
    }
  }
  for (const json::Member& m : store->members()) {
    const std::string& k = m.key;
    if (k.size() < 3 || k.size() > 18 || k[0] != '0' || k[1] != 'x' || (k[2] == '0' && k.size() > 3)) continue;
    if (std::any_of(k.begin() + 2, k.end(), [](char c) { return !(std::isdigit(static_cast<unsigned char>(c)) ||
                                                                   (c >= 'a' && c <= 'f')); })) {
      continue;
    }
    uint64_t id = gd::hex_id(k);
    if (id < v1_21::scores::PVP_KEYS || !m.value.is_array()) continue;
    const bool lo = lower.count(id) > 0;
    std::optional<int64_t> best;
    for (const Value& e : m.value.items()) {
      if (!e.is_array() || e.size() != 3) continue;
      if (!e.items()[0].is_int() || !e.items()[1].is_int() || !e.items()[2].is_int()) continue;
      std::optional<uint64_t> who = e.items()[0].as_u64();
      std::optional<int64_t> score = e.items()[1].as_i64(), at = e.items()[2].as_i64();
      if (!who || !score || !at || *who != player || *score <= 0 || *score >= v1_21::scores::U32 || *at < 0) continue;
      if (!best || (lo ? *score < *best : *score > *best)) best = *score;
    }
    if (best) out[id] = *best;
  }
  return out;
}

}
