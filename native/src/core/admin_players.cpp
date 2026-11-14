#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/admin.h"
#include "core/core.h"
#include "core/env.h"
#include "core/util.h"
#include "core/v1_21/handlers.h"
#include "core/v1_21/player.h"
#include "core/v1_21/relay.h"

namespace tcr::admin {

namespace {

using json::Value;

Value number_or_null(const Value* v) {
  if (v && (v->is_int() || v->is_float())) return *v;
  return Value();
}

size_t length(const Value* v) { return v && (v->is_array() || v->is_object()) ? v->size() : 0; }

Value id_text(const Value& v) {
  std::optional<uint64_t> u = v.as_u64();
  if (!u || *u == v1_21::NO_ID || *u == 0xFFFFFFFFull) return Value();
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(*u));
  return Value::string(buf);
}

Value position(const Value* v) {
  if (!v || !v->is_array() || v->size() != 3) return Value();
  Value out = Value::array();
  bool any = false;
  for (const Value& w : v->items()) {
    std::optional<uint64_t> u = w.as_u64();
    if (!u || *u > 0xFFFFFFFFull) return Value();
    any |= *u != 0;
    uint32_t bits = static_cast<uint32_t>(*u);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    if (f != f) return Value();
    out.push(Value::number(static_cast<double>(f)));
  }
  return any ? out : Value();
}

void perks(const Value* v, Value* out) {
  Value list = Value::array();
  int64_t total = 0;
  if (v && v->is_array()) {
    for (const Value& p : v->items()) {
      std::optional<int64_t> n = p.as_i64();
      list.push(n ? Value::integer(*n) : Value());
      if (n) total += *n;
    }
  }
  out->set("perks", list);
  out->set("perk_total", Value::integer(total));
}

Value summary(const Value& save) {
  Value out = Value::object();
  out.set("level", number_or_null(save.get("level")));
  out.set("xp", number_or_null(save.get("xp")));
  out.set("bucks", number_or_null(save.get("bucks")));
  out.set("crew_credits", number_or_null(save.get("crew_credits")));
  out.set("perk_points", number_or_null(save.get("perk_points")));
  const Value* ids = save.get("ids");
  out.set("car", ids && ids->is_array() && ids->size() >= 1 ? id_text(ids->items()[0]) : Value());
  out.set("garage", Value::uinteger(length(save.get("garage"))));
  out.set("missions_done", Value::uinteger(length(save.get("missions_done"))));
  out.set("activities_done", Value::uinteger(length(save.get("activities_done"))));
  out.set("collectibles", Value::uinteger(length(save.get("collectibles"))));
  perks(save.get("perks"), &out);
  out.set("faction", number_or_null(save.get("group")));
  out.set("faction_joined", number_or_null(save.get("group_joined")));
  out.set("zone", number_or_null(save.get("zone")));
  out.set("position", position(save.get("position")));
  return out;
}

Value snapshot(const v1_21::Player& p) {
  Value s = Value::object();
  s.set("level", Value::integer(p.level));
  s.set("xp", p.xp);
  s.set("bucks", p.bucks);
  s.set("crew_credits", Value::integer(p.crew_credits));
  s.set("perk_points", Value::integer(p.perk_points));
  s.set("ids", p.ids);
  s.set("perks", p.perks);
  s.set("group", p.group);
  s.set("group_joined", p.group_joined);
  s.set("zone", p.zone);
  s.set("position", p.position);
  Value out = summary(s);
  out.set("garage", Value::uinteger(length(&p.garage)));
  out.set("missions_done", Value::uinteger(length(&p.missions_done)));
  out.set("activities_done", Value::uinteger(length(&p.activities_done)));
  out.set("collectibles", Value::uinteger(length(&p.collectibles)));
  return out;
}

}

json::Value players(Core& core, const std::vector<RelayView>& relays) {
  Value out = Value::array();
  const double wall = core.env().time();
  const double mono = core.env().monotonic();
  for (const RelayView& r : relays) {
    if (!r.relay) continue;
    for (const auto& [addr, sess] : r.relay->sessions()) {
      if (!sess->connected || !sess->name || !sess->ctx) continue;
      Value e = snapshot(sess->ctx->player);
      e.set("version", Value::string(r.version));
      e.set("name", Value::string(*sess->name));
      e.set("address", Value::string(addr.ip + ":" +
                                     (addr.port_text.empty() ? std::to_string(addr.port) : addr.port_text)));
      Value platform;
      if (sess->platform.is_string()) {
        const std::string& p = sess->platform.as_string();
        platform = Value::string(p == "uplay" ? "PC" : p == "psn" ? "PS4" : p);
      }
      e.set("platform", platform);
      e.set("since", sess->stamp > 0 ? Value::number(sess->stamp) : Value());
      e.set("last_seen", Value::number(wall - std::max(0.0, mono - sess->seen)));
      e.set("crew", sess->ctx->crew_group ? id_text(Value::uinteger(sess->ctx->crew_group)) : Value());
      out.push(e);
    }
  }
  return out;
}

json::Value saved_player(const std::string& data) {
  std::optional<Value> save = json::loads_utf8(data, json::kHttpMaxDepth);
  if (!save || !save->is_object()) {
    Value e = Value::object();
    e.set("error", Value::string("the save is no JSON object"));
    return e;
  }
  return summary(*save);
}

}
