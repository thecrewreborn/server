#include "debug_peers.h"

#include <cstring>

#include "core/json.h"
#include "core/pymath.h"
#include "core/util.h"
#include "core/v1_21/handlers.h"
#include "core/v1_21/peers.h"
#include "core/wire.h"

namespace tcr {
namespace debug {

namespace {

using json::Value;
namespace pr = v1_21::peers;

Value S(const std::string& s) { return Value::string(s); }
Value H(const Bytes& b) { return Value::string(py::hex(b)); }

Value D(double d) {
  uint64_t bits;
  std::memcpy(&bits, &d, sizeof(bits));
  Bytes b;
  wire::put_u64(b, bits);
  return H(b);
}

std::string raised_text(const Raised& r) { return r.type + ": " + r.text; }

struct In {
  const Value& v;
  bool ok = true;

  const Value* get(const char* k) {
    const Value* x = v.get(k);
    if (!x) ok = false;
    return x;
  }
  std::string str(const char* k) {
    const Value* x = get(k);
    if (!x || !x->is_string()) {
      ok = false;
      return std::string();
    }
    return x->as_string();
  }
  Bytes hex(const char* k) {
    std::optional<Bytes> b = py::fromhex(str(k));
    if (!b) ok = false;
    return b ? *b : Bytes();
  }
  double dbl(const char* k) {
    Bytes b = hex(k);
    if (b.size() != 8) {
      ok = false;
      return 0.0;
    }
    uint64_t bits = wire::get_u64(b.data());
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
  }
  int64_t i64(const char* k) {
    const Value* x = get(k);
    std::optional<int64_t> i = x ? x->as_i64() : std::nullopt;
    if (!i) ok = false;
    return i ? *i : 0;
  }
  uint64_t u64(const char* k) {
    const Value* x = get(k);
    std::optional<uint64_t> u = x ? x->as_u64() : std::nullopt;
    if (!u) ok = false;
    return u ? *u : 0;
  }
  std::optional<uint64_t> u64_or_null(const char* k) {
    const Value* x = get(k);
    if (!x || x->is_null()) return std::nullopt;
    return u64(k);
  }
  bool flag(const char* k) {
    const Value* x = get(k);
    if (!x || !x->is_bool()) {
      ok = false;
      return false;
    }
    return x->as_bool();
  }
};

std::optional<v1_21::Ctx> ctx_from(Core& core, In& a, const char* key) {
  const Value* c = a.get(key);
  if (!c || c->is_null()) return std::nullopt;
  In x{*c};
  v1_21::Ctx ctx;
  ctx.core = &core;
  ctx.verified = x.flag("verified");
  ctx.player_id = x.u64("player_id");
  ctx.player_name = x.str("player_name");
  ctx.client_ip = x.str("client_ip");
  if (!x.ok) a.ok = false;
  return ctx;
}

Value pid_value(const std::optional<uint64_t>& pid) { return pid ? Value::uinteger(*pid) : Value(); }

Value peer_value(const pr::Peer& p) {
  Value v = Value::object();
  v.set("player_id", Value::uinteger(p.player_id));
  v.set("info", H(p.info));
  v.set("entity", H(p.entity));
  v.set("x", D(p.x));
  v.set("y", D(p.y));
  v.set("seen", D(p.seen));
  v.set("order", Value::integer(p.order));
  return v;
}

bool settings_from(In& a, discovery::Settings* s) {
  const Value* v = a.get("settings");
  if (!v) return false;
  In x{*v};
  s->relay_port = x.i64("relay_port");
  s->http_port = x.i64("http_port");
  const Value* adv = x.get("advertise");
  if (adv && adv->is_string()) {
    s->advertise = adv->as_string();
  } else {
    s->advertise.reset();
  }
  s->bots = x.flag("bots");
  s->afk = x.flag("afk");
  s->https_port = x.i64("https_port");
  s->dlc = x.flag("dlc");
  s->server_id = x.hex("server_id");
  if (!x.ok) a.ok = false;
  return x.ok;
}

void routes_from(In& a, TableRoute* route) {
  route->table.clear();
  const Value* v = a.get("routes");
  if (!v || !v->is_object()) {
    a.ok = false;
    return;
  }
  for (const json::Member& m : v->members()) {
    if (m.value.is_string()) route->table[m.key] = m.value.as_string();
  }
}

class RecordingSender final : public discovery::Sender {
 public:
  explicit RecordingSender(bool fail) : fail_(fail) {}
  bool send(const Bytes& reply) override {
    if (fail_) return false;
    sent.push_back(reply);
    return true;
  }
  std::vector<Bytes> sent;

 private:
  bool fail_;
};

}

bool peers_call(Core& core, PeersState& state, const std::string& name, const std::string& args_json,
                std::string* result) {
  std::optional<Value> parsed = json::loads_utf8(args_json);
  if (!parsed || !parsed->is_object()) {
    *result = "arguments are not a JSON object";
    return false;
  }
  In a{*parsed};
  Value r = Value::object();
  pr::Peers& peers = core.peers();
  Raised raised;
  if (name == "settings") {
    const Value* m = a.get("multiplayer");
    const Value* l = a.get("link_home");
    if (m && m->is_string()) {
      core.overrides().multiplayer = m->as_string();
    } else {
      core.overrides().multiplayer.reset();
    }
    if (l && l->is_bool()) {
      core.overrides().link_home = l->as_bool();
    } else {
      core.overrides().link_home.reset();
    }
  } else if (name == "clear") {
    peers.clear();
  } else if (name == "table") {
    Value list = Value::array();
    for (const pr::Peer& p : peers.all()) list.push(peer_value(p));
    r.set("peers", list);
    r.set("count", Value::integer(peers.count()));
  } else if (name == "register") {
    Bytes body = a.hex("body");
    std::optional<v1_21::Ctx> ctx = ctx_from(core, a, "ctx");
    std::optional<uint64_t> pid;
    if (a.ok && pr::register_peer(core, body, ctx ? &*ctx : nullptr, &pid, &raised)) {
      r.set("pid", pid_value(pid));
    } else {
      r.set("raised", S(raised_text(raised)));
    }
  } else if (name == "handle") {
    uint32_t id = static_cast<uint32_t>(a.u64("id"));
    Bytes body = a.hex("body");
    std::optional<v1_21::Ctx> ctx = ctx_from(core, a, "ctx");
    if (!ctx) {
      a.ok = false;
    } else {
      v1_21::HandleResult h = v1_21::handle(*ctx, id, body);
      switch (h.kind) {
        case v1_21::HandleResult::kFrame:
          r.set("frame", H(h.frame));
          break;
        case v1_21::HandleResult::kRaised:
          r.set("raised", S(raised_text(h.raised)));
          break;
        case v1_21::HandleResult::kNoReply:
          r.set("none", Value::boolean(true));
          break;
        case v1_21::HandleResult::kUnknown:
          r.set("unknown", Value::boolean(true));
          break;
      }
    }
  } else if (name == "session_reply" || name == "nearby_reply") {
    std::optional<uint64_t> pid = a.u64_or_null("pid");
    double x = a.dbl("x"), y = a.dbl("y");
    Bytes out;
    bool ok = a.ok && (name == "session_reply" ? pr::session_reply(core, pid, x, y, &out, &raised)
                                               : pr::nearby_reply(core, pid, x, y, &out, &raised));
    if (ok) {
      r.set("reply", H(out));
    } else {
      r.set("raised", S(raised_text(raised)));
    }
  } else if (name == "session_join_reply") {
    Bytes body = a.hex("body");
    std::optional<v1_21::Ctx> ctx = ctx_from(core, a, "ctx");
    Bytes out;
    if (a.ok && pr::session_join_reply(core, body, ctx ? &*ctx : nullptr, &out, &raised)) {
      r.set("reply", H(out));
    } else {
      r.set("raised", S(raised_text(raised)));
    }
  } else if (name == "forget") {
    peers.forget(a.u64("pid"));
  } else if (name == "moved") {
    peers.moved(a.u64("pid"), a.dbl("x"), a.dbl("y"));
  } else if (name == "others") {
    Value list = Value::array();
    for (const auto& e : peers.others(a.u64("pid"), a.dbl("x"), a.dbl("y"))) {
      Value pair = Value::array();
      pair.push(D(e.first));
      pair.push(Value::uinteger(peers.all()[e.second].player_id));
      list.push(pair);
    }
    r.set("others", list);
  } else if (name == "hosts") {
    Value list = Value::array();
    for (size_t i : peers.hosts(a.u64("pid"))) list.push(Value::uinteger(peers.all()[i].player_id));
    r.set("hosts", list);
  } else if (name == "element") {
    const pr::Peer* p = peers.get(a.u64("pid"));
    std::optional<uint64_t> viewer = a.u64_or_null("viewer");
    const pr::Peer* v = viewer ? peers.get(*viewer) : nullptr;
    r.set("element", p ? H(p->element(v)) : Value());
  } else if (name == "peer") {
    const pr::Peer* p = peers.get(a.u64("pid"));
    if (p) {
      pr::Platform pl = p->platform();
      r.set("key", Value::uinteger(p->key()));
      r.set("platform", pl == pr::Platform::kPs4 ? S("ps4") : pl == pr::Platform::kPc ? S("pc") : Value());
      r.set("name_hex", S(py::hex(p->name())));
    }
  } else if (name == "entity_properties") {
    Value list = Value::array();
    for (const auto& prop : pr::entity_properties(a.hex("entity"))) {
      Value pair = Value::array();
      pair.push(Value::uinteger(prop.first));
      pair.push(H(prop.second));
      list.push(pair);
    }
    r.set("props", list);
  } else if (name == "platform_property") {
    std::string platform = a.str("platform");
    Bytes nm = a.hex("name");
    auto prop = pr::platform_property(platform == "pc" ? pr::Platform::kPc : pr::Platform::kPs4,
                                      std::string(nm.begin(), nm.end()));
    r.set("type", Value::uinteger(prop.first));
    r.set("payload", H(prop.second));
  } else if (name == "hypot") {
    r.set("r", D(py::hypot(a.dbl("x"), a.dbl("y"))));
  } else if (name == "pack_f32") {
    Bytes out;
    if (py::pack_f32(out, a.dbl("x"), &raised)) {
      r.set("r", H(out));
    } else {
      r.set("raised", S(raised_text(raised)));
    }
  } else if (name == "utf16") {
    Bytes data = a.hex("data");
    r.set("r", S(py::hex(py::decode_utf16le_replace(data.data(), data.size()))));
  } else if (name == "utf16_encode") {
    Bytes text = a.hex("text");
    r.set("r", H(py::encode_utf16le(std::string(text.begin(), text.end()))));
  } else if (name == "sort") {
    const Value* keys = a.get("keys");
    std::vector<std::pair<double, int64_t>> items;
    if (keys && keys->is_array()) {
      for (const Value& k : keys->items()) {
        Bytes b = k.is_string() ? py::fromhex(k.as_string()).value_or(Bytes()) : Bytes();
        if (b.size() != 8) {
          a.ok = false;
          break;
        }
        uint64_t bits = wire::get_u64(b.data());
        double d;
        std::memcpy(&d, &bits, sizeof(d));
        items.emplace_back(d, static_cast<int64_t>(items.size()));
      }
    }
    py::list_sort(items, [](const std::pair<double, int64_t>& x, const std::pair<double, int64_t>& y) {
      return x.first < y.first;
    });
    Value list = Value::array();
    for (const auto& e : items) list.push(Value::integer(e.second));
    r.set("order", list);
  } else if (name == "discovery.answer") {
    discovery::Settings s;
    settings_from(a, &s);
    TableRoute route;
    routes_from(a, &route);
    Bytes probe = a.hex("probe");
    std::string peer = a.str("peer");
    Bytes reply;
    discovery::Answer k = discovery::answer(probe, peer, s, route, &reply, &raised);
    if (k == discovery::Answer::kReply) {
      r.set("reply", H(reply));
    } else if (k == discovery::Answer::kRaised) {
      r.set("raised", S(raised.type));
    }
  } else if (name == "discovery.client_text") {
    r.set("r", S(discovery::client_text(a.hex("data"))));
  } else if (name == "discovery.new") {
    discovery::Settings s;
    settings_from(a, &s);
    routes_from(a, &state.route);
    if (a.ok) state.discovery.reset(new discovery::Discovery(core.env(), s, state.route));
  } else if (name == "discovery.datagram") {
    Bytes data = a.hex("data");
    std::string ip = a.str("ip");
    RecordingSender send(a.flag("send_fails"));
    if (!state.discovery) {
      *result = "no discovery (discovery.new first)";
      return false;
    }
    if (a.ok) state.discovery->datagram(data, ip, send);
    Value list = Value::array();
    for (const Bytes& b : send.sent) list.push(H(b));
    r.set("sent", list);
  } else {
    *result = "unknown debug call " + name;
    return false;
  }
  if (!a.ok) {
    *result = "missing or wrong arguments for " + name;
    return false;
  }
  *result = json::dumps(r);
  return true;
}

}
}
