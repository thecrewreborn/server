#include "core/v1_21/peers.h"

#include <algorithm>
#include <cstring>

#include "core/config.h"
#include "core/core.h"
#include "core/pymath.h"
#include "core/ubi.h"
#include "core/v1_21/handlers.h"
#include "core/v1_21/pack.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {
namespace peers {

namespace {

Bytes slice(const Bytes& b, uint64_t from, uint64_t to) {
  uint64_t n = b.size();
  if (from > n) from = n;
  if (to > n) to = n;
  if (to <= from) return Bytes();
  return Bytes(b.begin() + static_cast<ptrdiff_t>(from), b.begin() + static_cast<ptrdiff_t>(to));
}

void append(Bytes& out, const Bytes& more) { out.insert(out.end(), more.begin(), more.end()); }

uint32_t u32_at(const Bytes& b, uint64_t p) { return wire::get_u32(b.data() + p); }

bool need(const Bytes& b, uint64_t p, size_t size, Raised* raised) {
  return pack::unpack_check(b, static_cast<size_t>(p), size, raised);
}

Raised config_raised(const config::Error& e) { return {e.configparser() ? "configparser." + e.type : e.type, e.message}; }

bool link_home(Core& core, bool* on, Raised* raised) {
  if (core.overrides().link_home) {
    *on = *core.overrides().link_home;
    return true;
  }
  config::Result<bool> r = core.config().link_home();
  if (!r.ok()) {
    *raised = config_raised(r.error);
    return false;
  }
  *on = *r.value;
  return true;
}

Bytes array_of(const std::vector<Bytes>& elements) {
  Bytes body;
  wire::put_u32(body, static_cast<uint32_t>(elements.size()));
  for (const Bytes& e : elements) append(body, e);
  return wire::blob(wire::frame(wire::T_ARRAY, body));
}

Peers& table(Core& core) { return core.peers(); }

}

uint32_t Peer::key() const {
  if (info.size() < 8) return 0;
  return u32_at(info, info.size() - 8);
}

Platform Peer::platform() const {
  for (const auto& prop : entity_properties(entity)) {
    if (prop.first == P_ORBIS) return Platform::kPs4;
    if (prop.first == P_PUNCH) return Platform::kPc;
  }
  return Platform::kNone;
}

std::string Peer::name() const {
  if (info.size() < 8) return std::string();
  uint64_t p = 4 + 4 + uint64_t(u32_at(info, 4)) + 8 + 4 + 4 + 2 + 2 + 1 + 1;
  if (p + 4 > info.size()) return std::string();
  Bytes wide = slice(info, p + 4, p + 4 + u32_at(info, p));
  return py::decode_utf16le_replace(wide.data(), wide.size());
}

Bytes Peer::element(const Peer* viewer) const {
  Platform mine = platform();
  Platform theirs = viewer ? viewer->platform() : Platform::kNone;
  bool crossing = viewer && theirs != Platform::kNone && mine != Platform::kNone && mine != theirs;
  Bytes out;
  wire::put_u32(out, SESSION_NUMBER);
  out.insert(out.end(), info.begin() + std::min<size_t>(4, info.size()), info.end());
  if (crossing && out.size() >= 8) {
    Bytes tail(out.end() - 4, out.end());
    out.resize(out.size() - 8);
    wire::put_u32(out, viewer->key());
    append(out, tail);
  }
  std::vector<std::pair<uint32_t, Bytes>> props;
  bool changed = false;
  for (auto& prop : entity_properties(entity)) {
    if (prop.first == P_PEER) {
      prop.second = out;
      changed = true;
    } else if (crossing && (prop.first == P_ORBIS || prop.first == P_PUNCH)) {
      prop = platform_property(theirs, name());
      changed = true;
    }
    props.push_back(std::move(prop));
  }
  append(out, wire::blob(changed ? net_entity(props) : entity));
  return out;
}

double Peers::clock() const { return core_.env().monotonic(); }

Peer* Peers::find(uint64_t pid) {
  for (Peer& p : by_id_) {
    if (p.player_id == pid) return &p;
  }
  return nullptr;
}

const Peer* Peers::get(uint64_t pid) const {
  for (const Peer& p : by_id_) {
    if (p.player_id == pid) return &p;
  }
  return nullptr;
}

void Peers::add(uint64_t pid, Bytes info, Bytes entity, double x, double y) {
  double now = clock();
  std::vector<Peer> kept;
  kept.reserve(by_id_.size());
  for (Peer& p : by_id_) {
    if (!(now - p.seen > STALE_S)) kept.push_back(std::move(p));
  }
  by_id_.swap(kept);
  Peer* old = find(pid);
  int64_t order;
  if (old && clock() - old->seen <= STALE_S) {
    order = old->order;
  } else {
    order = ++count_;
  }
  Peer p;
  p.player_id = pid;
  p.info = std::move(info);
  p.entity = std::move(entity);
  p.x = x;
  p.y = y;
  p.seen = clock();
  p.order = order;
  if (old) {
    *old = std::move(p);
  } else {
    by_id_.push_back(std::move(p));
  }
}

void Peers::forget(uint64_t pid) {
  for (auto it = by_id_.begin(); it != by_id_.end(); ++it) {
    if (it->player_id == pid) {
      by_id_.erase(it);
      return;
    }
  }
}

void Peers::moved(uint64_t pid, double x, double y) {
  if (Peer* p = find(pid)) {
    p->x = x;
    p->y = y;
    p->seen = clock();
  }
}

std::vector<std::pair<double, size_t>> Peers::others(uint64_t pid, double x, double y) const {
  double now = clock();
  std::vector<std::pair<double, size_t>> near;
  for (size_t i = 0; i < by_id_.size(); i++) {
    const Peer& p = by_id_[i];
    if (p.player_id != pid && now - p.seen <= STALE_S) near.emplace_back(py::hypot(p.x - x, p.y - y), i);
  }
  py::list_sort(near, [](const std::pair<double, size_t>& a, const std::pair<double, size_t>& b) {
    return a.first < b.first;
  });
  if (near.size() > MAX_PEERS) near.resize(MAX_PEERS);
  return near;
}

std::vector<size_t> Peers::hosts(uint64_t pid) const {
  double now = clock();
  const Peer* me = get(pid);
  std::vector<size_t> older;
  for (size_t i = 0; i < by_id_.size(); i++) {
    const Peer& p = by_id_[i];
    if (p.player_id != pid && now - p.seen <= STALE_S && (!me || p.order < me->order)) older.push_back(i);
  }
  py::list_sort(older, [this](size_t a, size_t b) { return by_id_[a].order < by_id_[b].order; });
  if (older.size() > MAX_PEERS) older.resize(MAX_PEERS);
  return older;
}

std::vector<std::pair<uint32_t, Bytes>> entity_properties(const Bytes& entity) {
  std::vector<std::pair<uint32_t, Bytes>> props;
  uint64_t p = 12;
  while (p + 16 <= entity.size()) {
    uint32_t n = u32_at(entity, p);
    uint32_t size = u32_at(entity, p + 4), kind = u32_at(entity, p + 8);
    props.emplace_back(kind, slice(entity, p + 16, p + 4 + size));
    p += 4 + uint64_t(n);
  }
  return props;
}

Bytes net_entity(const std::vector<std::pair<uint32_t, Bytes>>& props) {
  Bytes body;
  for (const auto& prop : props) append(body, wire::blob(wire::frame(prop.first, prop.second)));
  return wire::frame(NET_ENTITY, body);
}

std::pair<uint32_t, Bytes> platform_property(Platform platform, const std::string& name) {
  Bytes out;
  if (platform == Platform::kPc) {
    out.assign(8, 0);
    out.push_back(0x08);
    std::string id = ubi::stable_id("profile", name);
    append(out, wire::blob(Bytes(id.begin(), id.end())));
    out.insert(out.end(), 28, 0);
    return {P_PUNCH, out};
  }
  for (uint32_t cp : py::code_points(name)) {
    if (out.size() == 16) break;
    out.push_back(cp < 0x80 ? static_cast<uint8_t>(cp) : static_cast<uint8_t>('?'));
  }
  out.resize(44, 0);
  return {P_ORBIS, out};
}

bool multiplayer(Core& core, std::string* mode, Raised* raised) {
  if (core.overrides().multiplayer) {
    *mode = *core.overrides().multiplayer;
    return true;
  }
  config::Result<std::string> r = core.config().multiplayer();
  if (!r.ok()) {
    *raised = config_raised(r.error);
    return false;
  }
  *mode = *r.value;
  return true;
}

bool enabled(Core& core, bool* on, Raised* raised) {
  std::string mode;
  if (!multiplayer(core, &mode, raised)) return false;
  *on = mode != "off";
  return true;
}

bool acting_id(Core& core, const Ctx& ctx, std::optional<uint64_t>* out, Raised* raised) {
  std::string mode;
  if (!multiplayer(core, &mode, raised)) return false;
  bool home = false;
  if (!link_home(core, &home, raised)) return false;
  bool lan_ok = home && ubi::trusted(ctx.client_ip);
  if (!ctx.verified || mode == "off" || (mode == "lan" && !lan_ok)) {
    out->reset();
  } else {
    *out = ctx.player_id;
  }
  return true;
}

bool info_end(const Bytes& body, size_t start, size_t* end, Raised* raised) {
  uint64_t p = uint64_t(start) + 4;
  if (!need(body, p, 4, raised)) return false;
  p += 4 + uint64_t(u32_at(body, p));
  p += 8 + 4 + 4 + 2 + 2 + 1 + 1;
  if (!need(body, p, 4, raised)) return false;
  p += 4 + uint64_t(u32_at(body, p));
  *end = static_cast<size_t>(p + 1 + 4 + 4);
  return true;
}

bool parse_register(const Bytes& body, Registration* out, Raised* raised) {
  if (!need(body, 0, 8, raised)) return false;
  out->y = pack::f32_at(body, 0);
  out->x = pack::f32_at(body, 4);
  uint64_t p = 12;
  if (!need(body, p, 4, raised)) return false;
  p += 4 + uint64_t(u32_at(body, p));
  if (!need(body, p, 8, raised)) return false;
  out->pid = wire::get_u64(body.data() + p);
  p += 8 + 4 + 4 + 2 + 2 + 1 + 1;
  if (!need(body, p, 4, raised)) return false;
  p += 4 + uint64_t(u32_at(body, p));
  p += 1 + 4 + 4;
  out->info = slice(body, 8, p);
  if (!need(body, p, 4, raised)) return false;
  uint32_t n = u32_at(body, p);
  out->entity = slice(body, p + 4, p + 4 + n);
  return true;
}

bool register_peer(Core& core, const Bytes& body, const Ctx* ctx, std::optional<uint64_t>* pid, Raised* raised) {
  Registration r;
  if (!parse_register(body, &r, raised)) return false;
  uint64_t id = r.pid;
  Bytes info = std::move(r.info);
  if (ctx) {
    std::optional<uint64_t> acting;
    if (!acting_id(core, *ctx, &acting, raised)) return false;
    if (!acting) {
      pid->reset();
      return true;
    }
    id = *acting;
    uint64_t at = 4 + 4 + uint64_t(u32_at(info, 4));
    Bytes next = slice(info, 0, at);
    wire::put_u64(next, id);
    append(next, slice(info, at + 8, info.size()));
    info.swap(next);
    at += 8 + 4 + 4 + 2 + 2 + 1 + 1;
    Bytes wide = py::encode_utf16le(ctx->player_name);
    uint64_t old_len = u32_at(info, at);
    next = slice(info, 0, at);
    wire::put_u32(next, static_cast<uint32_t>(wide.size()));
    append(next, wide);
    append(next, slice(info, at + 4 + old_len, info.size()));
    info.swap(next);
  }
  table(core).add(id, std::move(info), std::move(r.entity), r.x, r.y);
  *pid = id;
  return true;
}

bool session_reply(Core& core, std::optional<uint64_t> pid, double x, double y, Bytes* out, Raised* raised) {
  bool on = false;
  if (!enabled(core, &on, raised)) return false;
  if (!on || !pid) {
    *out = wire::empty_array();
    return true;
  }
  Peers& peers = table(core);
  peers.moved(*pid, x, y);
  const Peer* me = peers.get(*pid);
  std::vector<Bytes> elements;
  for (size_t i : peers.hosts(*pid)) elements.push_back(peers.all()[i].element(me));
  *out = array_of(elements);
  return true;
}

bool nearby_reply(Core& core, std::optional<uint64_t> pid, double x, double y, Bytes* out, Raised* raised) {
  bool on = false;
  if (!enabled(core, &on, raised)) return false;
  if (!on || !pid) {
    *out = wire::empty_array();
    return true;
  }
  Peers& peers = table(core);
  peers.moved(*pid, x, y);
  const Peer* me = peers.get(*pid);
  std::vector<Bytes> elements;
  for (const auto& e : peers.others(*pid, x, y)) {
    Bytes element;
    if (!py::pack_f32(element, e.first, raised)) return false;
    element.push_back(0);
    element.push_back(0);
    append(element, peers.all()[e.second].element(me));
    elements.push_back(std::move(element));
  }
  *out = array_of(elements);
  return true;
}

bool session_join_reply(Core& core, const Bytes& body, const Ctx* ctx, Bytes* out, Raised* raised) {
  size_t end = 0;
  if (!info_end(body, 0, &end, raised)) return false;
  Bytes me_info = slice(body, 0, end);
  if (!need(body, end, 8, raised)) return false;
  uint64_t host_id = wire::get_u64(body.data() + end);
  std::optional<uint64_t> me_id = wire::get_u64(me_info.data() + 4 + 4 + u32_at(me_info, 4));
  if (ctx && !acting_id(core, *ctx, &me_id, raised)) return false;
  Peers& peers = table(core);
  const Peer* me = me_id ? peers.get(*me_id) : nullptr;
  const Peer* host = peers.get(host_id);
  bool on = false;
  if (!enabled(core, &on, raised)) return false;
  if (!on || !me_id || !host) {
    *out = Bytes{0x00, 0x00};
    append(*out, me_info);
    append(*out, wire::blob(Bytes()));
    return true;
  }
  if (me) peers.moved(*me_id, me->x, me->y);
  *out = Bytes{0x01, 0x00};
  append(*out, host->element(me));
  return true;
}

}
}
}
