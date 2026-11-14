#include "core/ubi.h"

#include <algorithm>
#include <cstdio>

#include "core/core.h"
#include "core/logtext.h"
#include "core/util.h"
#include "core/version.h"
#include "mbedtls/md.h"

namespace tcr {
namespace ubi {

namespace {

const char* const kResourceNames[] = {
    "ContentFiltering", "FriendsProposal", "FriendsRequest", "FriendsLookup", "SocialfeedRead", "SocialfeedWrite",
    "Localization", "UplayWin", "UplayFriends", "EntitiesProfile", "EntitiesSpace", "Profiles", "ProfilesExternal",
    "Users", "Connection", "Messaging", "News", "Populations", "Event", "UplayPassport", "ExtendSession",
    "KeepAlive", "ApplicationUsed", "FixAccountIssues", "Everything"};

const char* const kGatewayResources[][2] = {
    {"all_connections", "/{env}/{version}/profiles/connections"},
    {"all_profiles/entities", "/{env}/{version}/profiles/entities"},
    {"all_spaces/entities", "/{env}/{version}/spaces/entities"},
    {"all_walls", "/{env}/{version}/profiles/wall"},
    {"certificates", "/{env}/{version}/certificates"},
    {"configs/events", "/{env}/{version}/spaces/{spaceId}/configs/events"},
    {"connections", "/{env}/{version}/profiles/{profileId}/connections"},
    {"events", "/{env}/{version}/profiles/{profileId}/events"},
    {"friends", "/{env}/{version}/profiles/me/friends"},
    {"news", "/{env}/{version}/profiles/me/news"},
    {"populations", "/{env}/{version}/profiles/me/populations"},
    {"profiles", "/{env}/{version}/profiles"},
    {"profiles/actions", "/{env}/{version}/profiles/{profileId}/actions"},
    {"profiles/applications", "/{env}/{version}/profiles/me/applications"},
    {"profiles/entities", "/{env}/{version}/profiles/{profileId}/entities"},
    {"profiles/rewards", "/{env}/{version}/profiles/{profileId}/rewards"},
    {"spaces/actions", "/{env}/{version}/spaces/{spaceId}/actions"},
    {"spaces/entities", "/{env}/{version}/spaces/{spaceId}/entities"},
    {"spaces/rewards", "/{env}/{version}/spaces/{spaceId}/rewards"},
    {"wall", "/{env}/{version}/profiles/{profileId}/wall"},
    {"wall/comments", "/{env}/{version}/profiles/wall/{postId}/comments"},
    {"wall/likes", "/{env}/{version}/profiles/wall/{postId}/likes"},
    {"wall/post", "/{env}/{version}/profiles/wall/{postId}"},
    {"websocket/server", "/{env}"}};

const char* const kEventTags[] = {
    "player.progression", "context.stop", "custom.PURCHASE", "custom.PHOTO_MODE_SCREENSHOT",
    "custom.PHOTO_MODE_END", "custom.TOKEN_UNLOCKED", "custom.SHOP_PURCHASE", "custom.KUDO_END",
    "custom.CAMERA_MODE_END", "custom.PVP_SESSION_END", "player.dlc", "custom.NETWORK_PROFILING_SESSION_END",
    "custom.SESSION_END", "custom.CHASE_END", "custom.NOTIFICATION_END", "custom.GAME_STOP",
    "custom.INGAME_STORE_SHOP_EXIT", "custom.SOCIAL_ACTIVITY", "custom.LOBBY_SEARCH_END",
    "custom.game.progression", "custom.ACCOUNT_CREATION", "custom.ACTIVITY_END", "game.localization",
    "custom.OBJECT_DISCOVERED", "custom.CREW_ACTION", "custom.PROFILING", "custom.HARDWARE_CONFIG",
    "custom.CHALLENGE_COMPLETE", "custom.PLAYER_POSITION", "player.achievement", "context.start",
    "custom.FREEDRIVE_CHALLENGE_END", "custom.CHALLENGE_START", "custom.LOBBY_PVP_EVENT_SELECTION_END",
    "custom.WELCOME_DASHBOARD", "custom.ACHIEVEMENT", "custom.FINE", "custom.MISSION_END", "player.start",
    "custom.FACTION_SALARY", "custom.SKILL_END", "custom.TRIAL_UPGRADE", "custom.LOOK_FOR_CREW_END",
    "game.start", "custom.achievement.unlock", "custom.COOP_STUNT_END", "custom.WHEEL_OF_FORTUNE_RESULT",
    "custom.DPOINT_VARIATION", "custom.PERK_UPGRADE", "custom.CAR_REPAIR", "custom.ACCESS_CARD_UNLOCKED",
    "custom.GRAPHICS_OPTIONS"};

const std::pair<const char*, int64_t> kPunch[] = {
    {"ALLOW_PLAY_WITHOUT_PUNCH", 1},        {"MAX_LOGIN_WAIT_TIME_MS", 120000},
    {"PUNCH_LOGIN_TIMEOUT_MS", 30000},      {"PUNCH_TRAVERSAL_KEEPALIVE_DTMS", 5000},
    {"PUNCH_TRAVERSAL_TIMEOUT_MS", 30000},  {"PUNCH_ACTION_TIMEOUT_MS", 30000},
    {"MAPPING_MAX_DURATION_MS", 300000}};

const uint8_t kIdNamespace[16] = {0x6f, 0x1d, 0x5c, 0x1e, 0x3c, 0x0b, 0x4a, 0x51,
                                  0x9a, 0x52, 0x74, 0x68, 0x65, 0x2d, 0x63, 0x72};

using json::Value;

Value str(const std::string& s) { return Value::string(s); }
Value num(int64_t i) { return Value::integer(i); }
Value obj(std::vector<json::Member> members) { return Value::object(std::move(members)); }
Value error_reply(const std::string& why) { return obj({{"error", str(why)}}); }

std::string uuid_text(const uint8_t* b) {
  std::string h = py::hex(b, 16);
  return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20);
}

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

bool ends_with(const std::string& s, const char* suffix) {
  size_t n = std::char_traits<char>::length(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool starts_with(const std::string& s, const char* prefix) {
  size_t n = std::char_traits<char>::length(prefix);
  return s.size() >= n && s.compare(0, n, prefix) == 0;
}

bool upper_is_offline(const std::string& s) {
  std::string up;
  for (uint32_t cp : py::code_points(s)) {
    if (cp < 0x80) {
      up += static_cast<char>(cp >= 'a' && cp <= 'z' ? cp - 'a' + 'A' : cp);
    } else if (cp == 0x131) {
      up += "I";
    } else if (cp == 0xdf) {
      up += "SS";
    } else if (cp == 0x17f) {
      up += "S";
    } else if (cp == 0xfb00) {
      up += "FF";
    } else if (cp == 0xfb01) {
      up += "FI";
    } else if (cp == 0xfb02) {
      up += "FL";
    } else if (cp == 0xfb03) {
      up += "FFI";
    } else if (cp == 0xfb04) {
      up += "FFL";
    } else if (cp == 0xfb05 || cp == 0xfb06) {
      up += "ST";
    } else {
      return false;
    }
    if (up.size() > 7) return false;
  }
  return up == "OFFLINE";
}

std::optional<uint32_t> parse_ipv4(const std::string& s) {
  if (s.find('/') != std::string::npos || s.empty()) return std::nullopt;
  uint32_t value = 0;
  int parts = 0;
  size_t i = 0;
  while (true) {
    size_t dot = s.find('.', i);
    std::string octet = s.substr(i, dot == std::string::npos ? std::string::npos : dot - i);
    if (octet.empty() || octet.size() > 3) return std::nullopt;
    for (char c : octet) {
      if (c < '0' || c > '9') return std::nullopt;
    }
    if (octet != "0" && octet[0] == '0') return std::nullopt;
    int v = 0;
    for (char c : octet) v = v * 10 + (c - '0');
    if (v > 255) return std::nullopt;
    value = value << 8 | static_cast<uint32_t>(v);
    parts++;
    if (dot == std::string::npos) break;
    i = dot + 1;
  }
  if (parts != 4) return std::nullopt;
  return value;
}

std::vector<std::string> split_on(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t i = 0;
  while (true) {
    size_t k = s.find(sep, i);
    out.push_back(s.substr(i, k == std::string::npos ? std::string::npos : k - i));
    if (k == std::string::npos) break;
    i = k + 1;
  }
  return out;
}

std::optional<std::pair<uint64_t, uint64_t>> parse_ipv6(const std::string& s) {
  if (s.empty() || s.find('/') != std::string::npos) return std::nullopt;
  std::vector<std::string> parts = split_on(s, ':');
  if (parts.size() < 3) return std::nullopt;
  if (parts.back().find('.') != std::string::npos) {
    std::optional<uint32_t> v4 = parse_ipv4(parts.back());
    if (!v4) return std::nullopt;
    parts.pop_back();
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%x", (*v4 >> 16) & 0xFFFF);
    parts.push_back(buf);
    std::snprintf(buf, sizeof(buf), "%x", *v4 & 0xFFFF);
    parts.push_back(buf);
  }
  if (parts.size() > 9) return std::nullopt;
  std::optional<size_t> skip;
  for (size_t i = 1; i + 1 < parts.size(); i++) {
    if (parts[i].empty()) {
      if (skip) return std::nullopt;
      skip = i;
    }
  }
  size_t hi = 0, lo = 0, skipped = 0;
  if (skip) {
    hi = *skip;
    lo = parts.size() - *skip - 1;
    if (parts.front().empty()) {
      if (--hi) return std::nullopt;
    }
    if (parts.back().empty()) {
      if (--lo) return std::nullopt;
    }
    if (hi + lo >= 8) return std::nullopt;
    skipped = 8 - (hi + lo);
  } else {
    if (parts.size() != 8 || parts.front().empty() || parts.back().empty()) return std::nullopt;
    hi = parts.size();
  }
  auto hextet = [](const std::string& h) -> std::optional<uint64_t> {
    if (h.empty() || h.size() > 4) return std::nullopt;
    uint64_t v = 0;
    for (char c : h) {
      int d = py::hex_value(static_cast<uint8_t>(c));
      if (d < 0) return std::nullopt;
      v = v * 16 + static_cast<uint64_t>(d);
    }
    return v;
  };
  std::vector<uint64_t> words;
  for (size_t i = 0; i < hi; i++) {
    std::optional<uint64_t> w = hextet(parts[i]);
    if (!w) return std::nullopt;
    words.push_back(*w);
  }
  for (size_t i = 0; i < skipped; i++) words.push_back(0);
  for (size_t i = parts.size() - lo; i < parts.size(); i++) {
    std::optional<uint64_t> w = hextet(parts[i]);
    if (!w) return std::nullopt;
    words.push_back(*w);
  }
  if (words.size() != 8) return std::nullopt;
  uint64_t high = 0, low = 0;
  for (int i = 0; i < 4; i++) high = high << 16 | words[i];
  for (int i = 4; i < 8; i++) low = low << 16 | words[i];
  return std::make_pair(high, low);
}

bool home_v4(uint32_t a) {
  return (a >> 24) == 10 || (a >> 20) == ((172u << 4) | 1) || (a >> 16) == ((192u << 8) | 168) || (a >> 24) == 127;
}

}

std::string stable_id(const std::string& kind, const std::string& name) {
  std::string msg(reinterpret_cast<const char*>(kIdNamespace), 16);
  msg += kind + ":" + name;
  uint8_t digest[20];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), reinterpret_cast<const uint8_t*>(msg.data()), msg.size(),
             digest);
  digest[6] = static_cast<uint8_t>((digest[6] & 0x0f) | 0x50);
  digest[8] = static_cast<uint8_t>((digest[8] & 0x3f) | 0x80);
  return uuid_text(digest);
}

std::string uuid4_text(const uint8_t* b) {
  uint8_t u[16];
  std::copy(b, b + 16, u);
  u[6] = static_cast<uint8_t>((u[6] & 0x0f) | 0x40);
  u[8] = static_cast<uint8_t>((u[8] & 0x3f) | 0x80);
  return uuid_text(u);
}

std::string ubi_time(const LocalTime& t) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06d0Z", t.year, t.month, t.day, t.hour, t.minute,
                t.second, t.microsecond);
  return buf;
}

std::string b64encode(const std::string& data) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  for (; i + 3 <= data.size(); i += 3) {
    uint32_t v = static_cast<uint8_t>(data[i]) << 16 | static_cast<uint8_t>(data[i + 1]) << 8 |
                 static_cast<uint8_t>(data[i + 2]);
    out += kAlphabet[v >> 18];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += kAlphabet[v & 63];
  }
  if (data.size() - i == 1) {
    uint32_t v = static_cast<uint8_t>(data[i]) << 16;
    out += kAlphabet[v >> 18];
    out += kAlphabet[(v >> 12) & 63];
    out += "==";
  } else if (data.size() - i == 2) {
    uint32_t v = static_cast<uint8_t>(data[i]) << 16 | static_cast<uint8_t>(data[i + 1]) << 8;
    out += kAlphabet[v >> 18];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

std::optional<std::string> b64decode_strict(const std::string& s) {
  auto value = [](uint8_t c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  for (char c : s) {
    if (static_cast<uint8_t>(c) >= 0x80) return std::nullopt;
  }
  if (!s.empty() && s[0] == '=') return std::nullopt;
  std::string out;
  int quad = 0, pads = 0;
  bool padding = false;
  uint32_t left = 0;
  for (size_t i = 0; i < s.size(); i++) {
    uint8_t c = static_cast<uint8_t>(s[i]);
    if (c == '=') {
      padding = true;
      if (quad == 0) return std::nullopt;
      if (quad >= 2 && quad + ++pads >= 4) {
        if (i + 1 < s.size()) return std::nullopt;
        return out;
      }
      continue;
    }
    int v = value(c);
    if (v < 0) return std::nullopt;
    if (padding) return std::nullopt;
    pads = 0;
    switch (quad) {
      case 0:
        quad = 1;
        left = static_cast<uint32_t>(v);
        break;
      case 1:
        quad = 2;
        out += static_cast<char>((left << 2) | (static_cast<uint32_t>(v) >> 4));
        left = static_cast<uint32_t>(v) & 0x0f;
        break;
      case 2:
        quad = 3;
        out += static_cast<char>((left << 4) | (static_cast<uint32_t>(v) >> 2));
        left = static_cast<uint32_t>(v) & 0x03;
        break;
      default:
        quad = 0;
        out += static_cast<char>((left << 6) | static_cast<uint32_t>(v));
        left = 0;
        break;
    }
  }
  if (quad != 0) return std::nullopt;
  return out;
}

std::vector<std::pair<std::string, std::string>> parse_qsl(const std::string& qs) {
  std::vector<std::pair<std::string, std::string>> out;
  if (qs.empty()) return out;
  for (const std::string& field : split_on(qs, '&')) {
    if (field.empty()) continue;
    size_t eq = field.find('=');
    if (eq == std::string::npos) continue;
    std::string name = field.substr(0, eq), value = field.substr(eq + 1);
    if (value.empty()) continue;
    std::replace(name.begin(), name.end(), '+', ' ');
    std::replace(value.begin(), value.end(), '+', ' ');
    out.emplace_back(py::unquote(name), py::unquote(value));
  }
  return out;
}

std::string ticket_name(const std::string& auth) {
  std::optional<AuthTicket> t = auth_ticket(auth);
  if (!t) return std::string();
  std::string first = t->ticket.substr(0, t->ticket.find('|'));
  return latin1_to_utf8(std::string(strip_ws_latin1(first)));
}

std::string credential(const std::string& auth) {
  std::optional<AuthTicket> t = auth_ticket(auth);
  if (!t) return std::string();
  if (ascii_lower(t->scheme) == "psn2.0") {
    std::string ticket(strip_ws_latin1(t->ticket));
    ticket += std::string((4 - ticket.size() % 4) % 4, '=');
    std::optional<std::string> raw = b64decode_strict(ticket);
    if (!raw) return std::string();
    std::string text;
    py::DecodeError err;
    if (!py::decode_utf8(reinterpret_cast<const uint8_t*>(raw->data()), raw->size(), false, &text, &err)) {
      return std::string();
    }
    std::string code;
    for (const auto& kv : parse_qsl(text)) {
      if (kv.first == "code") {
        code = kv.second;
        break;
      }
    }
    return upper_is_offline(code) ? std::string() : code;
  }
  size_t bar = t->ticket.find('|');
  return bar == std::string::npos ? std::string() : latin1_to_utf8(t->ticket.substr(bar + 1));
}

bool trusted(const std::string& ip) {
  std::string addr = ip.substr(0, ip.find('%'));
  if (std::optional<uint32_t> v4 = parse_ipv4(addr)) return home_v4(*v4);
  std::optional<std::pair<uint64_t, uint64_t>> v6 = parse_ipv6(addr);
  if (!v6) return false;
  if (v6->first == 0 && (v6->second >> 32) == 0xFFFF) return home_v4(static_cast<uint32_t>(v6->second));
  return v6->first == 0 && v6->second == 1;
}

bool name_allowed(const std::string& name) {
  if (name.empty() || name.size() > kNameMax) return false;
  for (char c : name) {
    if (c < ' ' || c > '~') return false;
  }
  return true;
}

std::pair<std::string, std::string> login_name(const json::Value& body, const std::string& auth) {
  const json::Value* given = body.get("nameOnPlatform");
  std::string name = given && given->is_string() ? given->as_string() : std::string();
  std::string platform = "psn";
  bool console = ascii_lower(std::string(strip_ws_latin1(auth))).compare(0, 6, "psn2.0") == 0;
  if (name.empty() && !console) {
    std::string from_ticket = ticket_name(auth);
    if (!from_ticket.empty()) {
      name = from_ticket;
      platform = "uplay";
    }
  }
  return {name.empty() ? std::string(kDefaultName) : name, platform};
}

json::Value loggable(const json::Value& reply) {
  if (!reply.is_object() || (!reply.contains("ticket") && !reply.contains("token"))) return reply;
  json::Value out = json::Value::object();
  for (const json::Member& m : reply.members()) {
    out.set(m.key, m.key == "ticket" || m.key == "token" ? json::Value::string("***") : m.value);
  }
  return out;
}

Ubi::Ubi(Core& core) : core_(core) {
  const std::string save = paths_.join(core.home(), "save");
  accounts_.reset(new AccountStore(core.env(), *core.files(), paths_.join(save, kAccountsStoreName),
                                   core.accounts_key()));
  server_id_.reset(new ServerId(core.env(), *core.files(), paths_.join(save, "server_id")));
  avatars_.reset(new avatar::Avatars(core.env(), *core.files(), paths_.join(save, "avatars"), paths_));
}

double Ubi::now() { return core_.env().monotonic(); }

void Ubi::print(const std::string& text) { core_.env().print_line(stamped(core_.env(), text)); }

void Ubi::say(const std::string& client_ip, const std::string& text, bool verified) {
  if (!verified) {
    std::string suppressed;
    bool allowed = lines_.allow(client_ip, now(), &suppressed);
    if (!suppressed.empty()) print(suppressed);
    if (!allowed) return;
  }
  print(text);
}

void Ubi::touch(const std::string& name, const std::string& client_ip) {
  online_.set(account_key(name), {client_ip, now()});
}

std::optional<std::string> Ubi::online_elsewhere(const std::string& name, const std::string& client_ip) {
  const std::pair<std::string, double>* entry = online_.get(account_key(name));
  if (entry && entry->first != client_ip && now() - entry->second < kOnlineTimeout) return entry->first;
  return std::nullopt;
}

void Ubi::logout(const std::string& client_ip) {
  for (auto it = online_.begin(); it != online_.end();) {
    it = it->second.first == client_ip ? online_.erase(it) : std::next(it);
  }
}

bool Ubi::expired(const Ticket& t, double now) {
  const double* seen = active_.get(account_key(t.name));
  return now - std::max(t.issued, seen ? *seen : -1e18) >= kTicketTtl;
}

void Ubi::active(const std::string& name) { active_.set(account_key(name), now()); }

std::optional<std::pair<std::string, std::string>> Ubi::ticket_owner(const std::string& ticket, bool refresh,
                                                                      Raised* raised) {
  double t = now();
  const Ticket* found = tickets_.get(ticket);
  if (!found || expired(*found, t)) return std::nullopt;
  Ticket entry = *found;
  Raised err;
  std::optional<std::string> claim = accounts_->account_id(entry.name, &err);
  if (!err.type.empty()) {
    if (raised) *raised = err;
    return std::nullopt;
  }
  if (!claim || claim != entry.claim) return std::nullopt;
  if (refresh) {
    Ticket* current = tickets_.get(ticket);
    if (current && *current == entry) current->issued = std::max(entry.issued, t);
  }
  return std::make_pair(entry.name, entry.platform);
}

bool Ubi::live_ticket(const std::string& auth, Raised* raised) {
  std::optional<AuthTicket> t = auth_ticket(auth);
  return t && ticket_owner(std::string(strip_ws_latin1(t->ticket)), false, raised).has_value();
}

bool Ubi::issue(const std::string& ticket, const std::string& name, const std::string& platform, Raised* raised) {
  double t = now();
  std::string account = account_key(name);
  std::optional<std::string> claim = accounts_->account_id(name, raised);
  if (!raised->type.empty()) return false;
  for (auto it = tickets_.begin(); it != tickets_.end();) {
    bool drop = expired(it->second, t) || account_key(it->second.name) == account;
    it = drop ? tickets_.erase(it) : std::next(it);
  }
  for (auto it = active_.begin(); it != active_.end();) {
    it = t - it->second >= kTicketTtl ? active_.erase(it) : std::next(it);
  }
  tickets_.set(ticket, Ticket{name, platform, t, claim});
  return true;
}

void Ubi::prune(Times& table, double window, double now) {
  for (auto it = table.begin(); it != table.end();) {
    std::vector<double> recent;
    for (double t : it->second) {
      if (now - t < window) recent.push_back(t);
    }
    if (recent.empty()) {
      it = table.erase(it);
    } else {
      it->second = std::move(recent);
      ++it;
    }
  }
}

std::optional<double> Ubi::take(Times& table, int limit, double window, const std::string& ip) {
  double t = now();
  prune(table, window, t);
  std::vector<double>& recent = table.setdefault(ip);
  if (static_cast<int>(recent.size()) >= limit) return std::nullopt;
  recent.push_back(t);
  return t;
}

void Ubi::give_back(Times& table, double window, const std::string& ip, double stamp) {
  if (std::vector<double>* times = table.get(ip)) {
    auto it = std::find(times->begin(), times->end(), stamp);
    if (it != times->end()) times->erase(it);
  }
  prune(table, window, now());
}

Reply Ubi::refused(const std::string& client_ip, const std::string& name, const std::string& why,
                   const std::string& short_why) {
  std::string suppressed;
  bool allowed = refusals_.allow(client_ip + std::string(1, '\0') + why, now(), &suppressed);
  if (!suppressed.empty()) print(suppressed);
  if (allowed) print("login " + client_ip + " refused: " + name + ": " + why);
  Reply r;
  r.status = 409;
  r.json = obj({{"error", str(name + ": " + (short_why.empty() ? why : short_why))}, {"errorCode", num(409)}});
  return r;
}

namespace {

Raised config_raised(const config::Error& e) {
  return {e.configparser() ? "configparser." + e.type : e.type, e.message};
}

}

bool Ubi::link_home(Raised* raised) {
  if (core_.overrides().link_home) return *core_.overrides().link_home;
  config::Result<bool> r = core_.config().link_home();
  if (!r.ok()) {
    *raised = config_raised(r.error);
    return false;
  }
  return *r.value;
}

std::optional<Reply> Ubi::check_login(const std::string& name, const std::string& platform, const std::string& auth,
                                      const std::string& client_ip, Raised* raised) {
  if (!name_allowed(name)) {
    return refused(client_ip, "this name", "name not allowed, use at most 32 letters, digits or signs of plain ASCII");
  }
  if (account_key(name) == account_key(kDefaultName)) {
    return refused(client_ip, "this name", "name not allowed, choose another");
  }
  std::optional<double> stamp = take(failed_, kFailLimit, kFailWindow, client_ip);
  if (!stamp) return refused(client_ip, name, "too many failed logins, try again later");
  std::optional<double> login = take(logins_, kLoginLimit, kLoginWindow, client_ip);
  if (!login) {
    give_back(failed_, kFailWindow, client_ip, *stamp);
    return refused(client_ip, name, "too many logins from this address, try again later");
  }
  std::optional<double> slot = take(claims_, kClaimLimit, kClaimWindow, client_ip);
  std::optional<double> shared;
  if (slot) shared = take(claims_all_, kClaimAllLimit, kClaimWindow, "");
  bool crowded = slot && !shared;
  if (crowded) {
    give_back(claims_, kClaimWindow, client_ip, *slot);
    slot.reset();
  }
  bool home = false;
  if (trusted(client_ip)) {
    Raised config_error;
    home = link_home(&config_error);
    if (!config_error.type.empty()) {
      *raised = config_error;
      return std::nullopt;
    }
  }
  Raised store_error;
  std::string verdict = accounts_->check(name, platform, credential(auth), slot.has_value(),
                                         home ? "always" : "window", &store_error);
  if (verdict != "wrong") give_back(failed_, kFailWindow, client_ip, *stamp);
  if (verdict != "ok" && verdict != "claimed" && verdict != "linked") {
    give_back(logins_, kLoginWindow, client_ip, *login);
  }
  if (slot && verdict != "claimed") {
    give_back(claims_, kClaimWindow, client_ip, *slot);
    give_back(claims_all_, kClaimWindow, "", *shared);
  }
  if (!store_error.type.empty()) {
    *raised = store_error;
    return std::nullopt;
  }
  if (verdict == "unclaimed") {
    return refused(client_ip, name,
                   crowded ? "too many new names right now, try again later"
                           : "too many new names from this address, try again later");
  }
  if (verdict == "wrong") {
    if (platform == "uplay" && account_key(name) == account_key("TheCrewReborn")) {
      return refused(client_ip, name, "wrong password",
                     "wrong password (this PC plays as the fallback name TheCrewReborn: set your own Name in "
                     "reborn.ini)");
    }
    return refused(client_ip, name, "wrong password");
  }
  if (verdict == "missing") {
    if (platform == "uplay") {
      return refused(client_ip, name,
                     "no device key: PC patch older than 0.0.4 or no server id yet (start the server first)");
    }
    return refused(client_ip, name,
                   "no console key: PS4 patch older than 0.0.4, the console found an older server first, UDP 4001 "
                   "not reachable, plain HTTP server, or the server changed after the game started (restart the "
                   "game)",
                   "no console key (PS4 patch older than 0.0.4, older server found first, UDP 4001 blocked, plain "
                   "HTTP server, or the server changed after the game started: restart the game)");
  }
  if (verdict == "unavailable") return refused(client_ip, name, "account store unavailable");
  if (verdict == "full") {
    std::string hint = "has " + std::to_string(kDevicesMax) +
                       " devices; the owner frees the name with tools/accounts.py free NAME, then its devices join "
                       "again";
    Reply r = refused(client_ip, name, hint);
    r.json.set("error", str(name + " " + hint));
    return r;
  }
  if (verdict == "linked") {
    say(client_ip,
        "login " + client_ip + ": " + name + ": new device linked (" + (home ? "home network" : "link window") + ")",
        true);
  }
  return std::nullopt;
}

Reply Ubi::console_picture(const std::string& name, const std::string& auth, const std::string& raw,
                           const std::string& client_ip) {
  Reply r;
  if (!console_uploads_.allow(client_ip, now())) {
    r.status = 429;
    r.json = error_reply("too many picture uploads, try again later");
    return r;
  }
  if (std::optional<std::string> why = avatar::check_console_picture(raw, false)) {
    r.status = raw.size() > avatar::kMaxUpload ? 413 : 400;
    r.json = error_reply(*why);
    return r;
  }
  std::string stripped(strip_ws_latin1(auth));
  size_t space = stripped.find(' ');
  std::string scheme = stripped.substr(0, space);
  std::string code =
      space == std::string::npos ? std::string() : std::string(strip_ws_latin1(stripped.substr(space + 1)));
  if (ascii_lower(scheme) != kConsoleAuth || code.empty()) {
    r.status = 401;
    r.json = error_reply("the console's login code is missing");
    return r;
  }
  Raised store_error;
  std::string verdict = accounts_->check(name, "psn", latin1_to_utf8(code), false, "never", &store_error);
  if (!store_error.type.empty()) {
    r.raised = true;
    r.exception = store_error;
    return r;
  }
  if (verdict == "unavailable") {
    r.status = 503;
    r.json = error_reply("account store unavailable");
    return r;
  }
  if (verdict != "ok") {
    r.status = 403;
    r.json = error_reply("only the console that claimed this name may set its picture");
    return r;
  }
  Raised raised;
  std::optional<std::string> why = avatars_->save_console_upload(name, raw, &raised);
  if (!why) {
    r.raised = true;
    r.exception = raised;
    return r;
  }
  if (!why->empty()) {
    r.status = 400;
    r.json = error_reply(*why);
    return r;
  }
  r.json = obj({{"saved", Value::boolean(true)}});
  return r;
}

json::Value Ubi::session(const json::Value& body, const std::string& client_ip, const std::string& auth,
                         Raised* raised) {
  std::pair<std::string, std::string> who = login_name(body, auth);
  const std::string& name = who.first;
  const std::string& platform = who.second;
  touch(name, client_ip);
  sessions_.set(client_ip, name);
  platforms_.set(client_ip, platform);
  std::string profile = stable_id("profile", name);
  profiles_.set(profile, {name, platform});
  int64_t sec = 0;
  int us = 0;
  if (!core_.env().now_utc(&sec, &us)) {
    *raised = Raised{"OverflowError", "timestamp out of range for platform time_t"};
    return Value();
  }
  uint8_t random[16];
  core_.env().urandom(random, sizeof(random));
  std::string ticket = b64encode("offline-ticket:" + uuid4_text(random));
  core_.env().urandom(random, sizeof(random));
  std::string token = b64encode("offline-token:" + uuid4_text(random));
  std::optional<std::string> shown = accounts_->display_name(name, raised);
  if (!raised->type.empty()) return Value();
  if (!issue(ticket, shown ? *shown : name, platform, raised)) return Value();
  Value reply = obj({{"platformType", str(platform)},
                     {"ticket", str(ticket)},
                     {"token", str(token)},
                     {"profileId", str(profile)},
                     {"userId", str(stable_id("user", name))},
                     {"nameOnPlatform", str(name)},
                     {"hasAcceptedLegalOptins", Value::boolean(true)},
                     {"spaceId", str(kSpaceId)},
                     {"productId", str(stable_id("product", "thecrew-ps4"))},
                     {"environment", str("Prod")},
                     {"expiration", str(ubi_time(civil_utc(sec + static_cast<int64_t>(kTicketTtl), us)))},
                     {"serverTime", str(ubi_time(civil_utc(sec, us)))},
                     {"clientIp", str(client_ip)},
                     {"initializeUser", Value::boolean(false)}});
  core_.env().urandom(random, sizeof(random));
  reply.set("sessionId", str(uuid4_text(random)));
  reply.set("accountIssues", Value::array());
  return reply;
}

json::Value Ubi::configuration(const std::string& host_in) {
  std::string host = http_port_ != 80 ? host_in + ":" + std::to_string(http_port_) : host_in;
  std::string base = "http://" + host;
  Value punch_values = Value::object();
  for (const auto& p : kPunch) punch_values.set(p.first, num(p.second));
  Value switches = Value::array();
  for (const char* n : kResourceNames) switches.push(obj({{"name", str(n)}, {"value", Value::boolean(true)}}));
  switches.push(obj({{"name", str("SendPopulationsInPlayerStart")}, {"value", Value::boolean(false)}}));
  switches.push(obj({{"name", str("Punch")}, {"value", punch_values}}));
  switches.push(obj({{"name", str("NATSOLVER_DEF_SOLVING_TIME_MS")}, {"value", num(30000)}}));
  switches.push(obj({{"name", str("MAX_TIME_PROBES")}, {"value", num(0)}}));
  Value resources = Value::array();
  for (const char* n : kResourceNames) {
    resources.push(obj({{"name", str(n)}, {"url", str(base + "/{version}")}, {"friendlyName", str(n)}}));
  }
  Value punch = obj({{"detectUrl1", str(base + "/detect")},
                     {"detectUrl2", str(base + "/detect")},
                     {"traversalUrl", str(base + "/traversal")}});
  for (const auto& p : kPunch) punch.set(p.first, num(p.second));
  Value gateway = Value::array();
  for (const auto& g : kGatewayResources) {
    std::string url = (std::string(g[0]) == "websocket/server" ? "ws://" + host : base) + g[1];
    gateway.push(obj({{"name", str(g[0])}, {"url", str(url)}, {"version", num(1)}}));
  }
  Value config = obj(
      {{"platformConfig", obj({{"uplayGameCode", str("")},
                               {"applicationId", str(kAppId)},
                               {"spaceId", str(kSpaceId)},
                               {"platform", str("psn")},
                               {"environment", str("Prod")}})},
       {"resources", resources},
       {"legacyUrls", Value::array()},
       {"sandboxes", Value::array()},
       {"uplayServices", Value::array()},
       {"punch", punch},
       {"sdkConfig", obj({{"timeoutSec", num(30)},
                          {"keepAliveTimeoutMin", num(10)},
                          {"ticketTTL", num(3600)},
                          {"lspPort", num(1200)},
                          {"httpSafetySleepTime", num(0)}})},
       {"featuresSwitches", switches},
       {"gatewayResources", gateway}});
  Value services = Value::array();
  services.push(obj({{"name", str("AvatarBaseUrl")}, {"url", str(base + "/avatars")}}));
  services.push(obj({{"name", str("DefaultAvatarUrl")}, {"url", str(base + "/avatars/default_avatar.png")}}));
  config.set("uplayServices", services);
  return obj({{"configuration", config}});
}

json::Value Ubi::profiles(const std::string& query) {
  Value found = Value::array();
  for (const auto& kv : parse_qsl(query)) {
    if (kv.first != "profileId") continue;
    for (const std::string& pid : split_on(kv.second, ',')) {
      if (pid.empty()) continue;
      const std::pair<std::string, std::string>* who = profiles_.get(pid);
      if (!who) continue;
      std::string user = stable_id("user", who->first);
      found.push(obj({{"profileId", str(pid)},
                      {"userId", str(user)},
                      {"platformType", str(who->second)},
                      {"idOnPlatform", str(who->second == "uplay" ? user : who->first)},
                      {"nameOnPlatform", str(who->first)}}));
    }
  }
  return obj({{"profiles", found}});
}

json::Value Ubi::version_reply() {
  std::optional<std::string> id = server_id_->hex_id();
  if (!id) return obj({{"server", str(kVersion)}});
  return obj({{"server", str(kVersion)}, {"server_id", str(*id)}});
}

Reply Ubi::route(const std::string& method, const std::string& full_path, const json::Value& body,
                 const std::string& host, const std::string& client_ip, int client_port, const std::string& auth,
                 const std::string& raw) {
  Reply r;
  auto raising = [](const Raised& why) {
    Reply e;
    e.raised = true;
    e.exception = why;
    return e;
  };
  size_t q = full_path.find('?');
  std::string path = full_path.substr(0, q);
  std::string query = q == std::string::npos ? std::string() : latin1_to_utf8(full_path.substr(q + 1));
  while (!path.empty() && path.back() == '/') path.pop_back();
  std::string name;
  if (path == "/version") {
    std::string client_version;
    for (const auto& kv : parse_qsl(query)) {
      if (kv.first == "client") client_version = kv.second;
    }
    if (!client_version.empty()) {
      Raised err;
      bool verified = live_ticket(auth, &err);
      if (!err.type.empty()) return raising(err);
      say(client_ip,
          "version " + client_ip + ": " + printable_text(client_version, 200) + ", server " + kVersion, verified);
    }
    r.json = version_reply();
    return r;
  }
  if (method == "GET" && ends_with(path, "/v2/profiles")) {
    r.json = profiles(query);
    return r;
  }
  if (avatar::match_name(path, "/avatars/console/", ".png", &name) && method == "PUT") {
    return console_picture(name, auth, raw, client_ip);
  }
  if (avatar::match_name(path, "/avatars/name/", ".rgba", &name) && method == "PUT") {
    const std::string* last = sessions_.get(client_ip);
    if (!last || *last != name) {
      r.status = 403;
      r.json = error_reply("only the logged-in player may set its picture");
      return r;
    }
    std::optional<bool> saved = avatars_->save_upload(name, raw, &r.exception);
    if (!saved) {
      r.raised = true;
      return r;
    }
    r.status = *saved ? 200 : 400;
    r.json = *saved ? obj({{"saved", Value::boolean(true)}}) : error_reply("need 256x256 RGBA");
    return r;
  }
  if (starts_with(path, "/avatars/profile/") && ends_with(path, ".rgba") && path.size() >= 17 + 36 + 1) {
    std::string pid = path.substr(17, 36);
    std::string rest = path.substr(17 + 36);
    bool id_ok = std::all_of(pid.begin(), pid.end(),
                             [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-'; });
    uint32_t size = rest == "/64.rgba" ? 64 : rest == "/128.rgba" ? 128 : rest == "/256.rgba" ? 256 : 0;
    if (id_ok && size) {
      const std::pair<std::string, std::string>* who = profiles_.get(pid);
      std::optional<std::string> who_name;
      if (who) who_name = who->first;
      Raised raised;
      std::optional<std::string> rgba = avatars_->rgba_for(who_name, size, client_ip, &raised);
      r.binary = true;
      if (!raised.type.empty()) {
        r.raised = true;
        r.exception = raised;
        return r;
      }
      r.status = rgba ? 200 : 503;
      r.bytes = rgba ? *rgba : std::string();
      return r;
    }
  }
  if (starts_with(path, "/avatars/") && ends_with(path, ".png")) {
    const std::string* last = sessions_.get(client_ip);
    std::optional<std::string> who;
    if (last) who = *last;
    Raised err;
    r.binary = true;
    r.bytes = avatars_->for_request(who, path, &err);
    if (!err.type.empty()) return raising(err);
    return r;
  }
  if (ends_with(path, "/profiles/sessions")) {
    if (method == "DELETE") {
      logout(client_ip);
      r.json = Value::object();
      return r;
    }
    if (method != "POST") {
      r.json = Value::object();
      return r;
    }
    std::pair<std::string, std::string> who = login_name(body, auth);
    Raised raised;
    std::optional<Reply> refusal = check_login(who.first, who.second, auth, client_ip, &raised);
    if (!raised.type.empty()) {
      r.raised = true;
      r.exception = raised;
      return r;
    }
    if (refusal) return *refusal;
    std::optional<std::string> other =
        who.first != kDefaultName ? online_elsewhere(who.first, client_ip) : std::nullopt;
    if (other) {
      print("login " + client_ip + " refused: " + who.first + " is playing on " + *other);
      r.status = 409;
      r.json = obj({{"error", str(who.first + " is already playing on another device")}, {"errorCode", num(409)}});
      return r;
    }
    Raised err;
    r.json = session(body, client_ip, auth, &err);
    if (!err.type.empty()) return raising(err);
    return r;
  }
  if (path.find("/applications/") != std::string::npos && ends_with(path, "/configuration")) {
    config::Result<std::optional<std::string>> advertised = core_.config().advertised_ip();
    if (!advertised.ok()) {
      r.raised = true;
      r.exception = config_raised(advertised.error);
      return r;
    }
    r.json = configuration(*advertised.value ? **advertised.value : host);
    return r;
  }
  if (ends_with(path, "/native/token")) {
    r.json = obj({{"access_token", str(b64encode("offline-shop-token"))},
                  {"token_type", str("bearer")},
                  {"expires_in", num(3600)}});
    return r;
  }
  if (ends_with(path, "/profiles/me/news")) {
    r.json = obj({{"news", Value::array()}});
    return r;
  }
  if (path.find("configs/events") != std::string::npos) {
    Value tags = Value::array();
    for (const char* t : kEventTags) tags.push(str(t));
    r.json = obj({{"config", obj({{"hmacEnabled", Value::boolean(false)}, {"sendPeriodSeconds", num(30)}})},
                  {"tags", tags}});
    return r;
  }
  if (path.find("populations") != std::string::npos) {
    Value set = obj({{"populationId", str(stable_id("population", "default"))},
                     {"platform", str("PS4")},
                     {"revisionId", num(1)},
                     {"currentPlayers", num(1)},
                     {"maxPlayers", num(1000)}});
    Value sets = Value::array();
    sets.push(set);
    r.json = obj({{"spaceId", str(kSpaceId)}, {"name", str("TheCrew")}, {"populationSets", sets}});
    return r;
  }
  if (ends_with(path, "/detect")) {
    r.json = obj({{"publicIp", str(client_ip)},
                  {"publicPort", num(client_port)},
                  {"natType", str("open")},
                  {"status", str("ok")}});
    return r;
  }
  if (ends_with(path, "/traversal")) {
    r.json = obj({{"status", str("ok")}, {"traversalPort", num(4000)}, {"relayPort", num(4000)}});
    return r;
  }
  if (path.find("entities") != std::string::npos || path.find("rewards") != std::string::npos ||
      path.find("actions") != std::string::npos) {
    r.json = obj({{"results", Value::array()}});
    return r;
  }
  r.json = Value::object();
  return r;
}

}
}
