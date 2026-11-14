#include "core/admin.h"

#include <algorithm>

#include "core/accounts.h"
#include "core/config.h"
#include "core/core.h"
#include "core/files.h"
#include "core/pyobj.h"
#include "core/ubi.h"
#include "core/util.h"
#include "core/v1_21/relay.h"

namespace tcr::admin {

namespace {

json::Value text(std::string s) { return json::Value::string(std::move(s)); }

json::Value as_text(const json::Value* v) {
  if (!v || v->is_null()) return json::Value();
  if (v->is_string()) return *v;
  return text(obj::str(*v));
}

json::Value as_time(const json::Value* v) {
  if (!v) return json::Value();
  if (v->is_float()) return v->as_float() != 0.0 ? json::Value::number(v->as_float()) : json::Value();
  if (v->is_int()) {
    std::optional<int64_t> n = v->as_i64();
    return n && *n != 0 ? json::Value::integer(*n) : json::Value();
  }
  return json::Value();
}

template <class T>
json::Value setting(const config::Result<T>& r, json::Value (*make)(const T&)) {
  return r.ok() ? make(*r.value) : json::Value();
}

json::Value from_string(const std::string& s) { return text(s); }
json::Value from_bool(const bool& b) { return json::Value::boolean(b); }

}

json::Value facts(Core& core, const Facts& f, const std::vector<RelayView>& relays) {
  json::Value out = json::Value::object();
  out.set("version", text(f.version));
  out.set("address", text(f.address));
  out.set("lan_ip", text(f.lan_ip));
  json::Value ports = json::Value::object();
  ports.set("http", json::Value::integer(f.http_port));
  ports.set("https", json::Value::integer(f.https_port));
  ports.set("discovery", json::Value::integer(f.discovery_port));
  ports.set("admin", json::Value::integer(f.admin_port));
  out.set("ports", ports);
  out.set("relay_ip", text(f.relay_ip));
  json::Value list = json::Value::array();
  for (const RelayView& r : relays) {
    json::Value e = json::Value::object();
    e.set("version", text(r.version));
    e.set("port", json::Value::integer(r.port));
    e.set("running", json::Value::boolean(r.relay != nullptr));
    list.push(e);
  }
  out.set("relays", list);
  out.set("server_id", text(f.server_id));
  const config::Config& c = core.config();
  out.set("multiplayer", setting(c.multiplayer(), from_string));
  out.set("dlc", setting(c.dlc(), from_string));
  out.set("public", setting(c.public_set(), from_bool));
  out.set("bots", setting(c.bots(), from_bool));
  out.set("afk_kick", setting(c.afk_kick(), from_bool));
  out.set("home", text(core.home()));
  out.set("started", json::Value::number(f.started_wall));
  out.set("uptime", json::Value::number(std::max(0.0, f.uptime)));
  return out;
}

json::Value clients(Core& core, const std::vector<RelayView>& relays) {
  json::Value out = json::Value::array();
  const double wall = core.env().time();
  const double mono = core.env().monotonic();
  for (const RelayView& r : relays) {
    if (!r.relay) continue;
    for (const auto& [addr, sess] : r.relay->sessions()) {
      if (!sess->connected) continue;
      json::Value e = json::Value::object();
      e.set("version", text(r.version));
      e.set("name", sess->name ? text(*sess->name) : json::Value());
      e.set("address", text(addr.ip + ":" + (addr.port_text.empty() ? std::to_string(addr.port) : addr.port_text)));
      json::Value platform;
      if (sess->platform.is_string()) {
        const std::string& p = sess->platform.as_string();
        platform = text(p == "uplay" ? "PC" : p == "psn" ? "PS4" : p);
      }
      e.set("platform", platform);
      e.set("since", sess->stamp > 0 ? json::Value::number(sess->stamp) : json::Value());
      e.set("last_seen", json::Value::number(wall - std::max(0.0, mono - sess->seen)));
      out.push(e);
    }
  }
  return out;
}

json::Value names(Core& core) {
  auto error = [](const std::string& why) {
    json::Value err = json::Value::object();
    err.set("error", text(why));
    return err;
  };
  AccountStore& store = core.ubi().accounts();
  json::Value data = json::Value::object();
  if (core.files()) {
    std::string raw;
    OsError err;
    if (core.files()->read(store.path(), &raw, &err)) {
      std::optional<json::Value> parsed = json::loads_utf8(raw, json::kHttpMaxDepth);
      if (!parsed || !parsed->is_object()) {
        return error(store.path() + " is damaged; the server moves it aside at the next login");
      }
      data = std::move(*parsed);
    } else if (!not_found(err)) {
      return error("could not read " + store.path() + ": " + err.text());
    }
  }
  Raised raised;
  json::Value entries = store.entries(&raised);
  if (!raised.type.empty()) return error(raised.line());
  json::Value out = json::Value::array();
  for (const json::Member& m : entries.members()) {
    const json::Value& v = m.value;
    json::Value e = json::Value::object();
    e.set("key", text(m.key));
    e.set("name", as_text(v.get("name")));
    e.set("platform", as_text(v.get("platform")));
    e.set("added", as_time(v.get("created")));
    const json::Value* devices = v.get("devices");
    e.set("devices", devices ? *devices : json::Value::integer(0));
    json::Value list = json::Value::array();
    const json::Value* entry = data.get(m.key);
    const json::Value* creds = entry && entry->is_object() ? entry->get("credentials") : nullptr;
    if (creds && creds->is_array()) {
      for (const json::Value& c : creds->items()) {
        if (!c.is_object()) continue;
        json::Value d = json::Value::object();
        d.set("platform", as_text(c.get("platform")));
        d.set("added", as_time(c.get("added")));
        list.push(d);
      }
    }
    e.set("device_list", list);
    e.set("link_until", as_time(v.get("link")));
    out.push(e);
  }
  return out;
}

std::string save_folder(Core& core, const RelayView& relay) {
  if (!relay.relay || relay.relay->save_path().empty() || !core.files()) return std::string();
  Files& files = *core.files();
  std::string abs;
  OsError err;
  if (!files.abspath(relay.relay->save_path(), &abs, &err)) return std::string();
  return files.join(files.dirname(abs), "players");
}

bool inside_home(Core& core, const std::string& path) {
  if (!core.files() || path.empty()) return false;
  Files& files = *core.files();
  std::string home, abs;
  OsError err;
  if (!files.abspath(core.home(), &home, &err) || !files.abspath(path, &abs, &err)) return false;
  if (abs == home) return true;
  if (abs.size() <= home.size() || abs.compare(0, home.size(), home) != 0) return false;
  char sep = abs[home.size()];
  char last = home.empty() ? '\0' : home.back();
  return sep == '/' || sep == '\\' || last == '/' || last == '\\';
}

bool save_file_name_ok(const std::string& file) {
  if (file.empty() || file.size() > 128 || file[0] == '.') return false;
  if (file.size() < 6 || file.compare(file.size() - 5, 5, ".json") != 0) return false;
  for (char c : file) {
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.';
    if (!ok) return false;
  }
  return true;
}

namespace {

std::vector<std::string> listing(Files& files, const std::string& folder) {
  std::vector<std::string> names, out;
  OsError err;
  if (folder.empty() || !files.listdir(folder, &names, &err)) return out;
  for (const std::string& n : names) {
    if (save_file_name_ok(n)) out.push_back(n);
  }
  std::sort(out.begin(), out.end());
  return out;
}

}

json::Value saves(Core& core, const std::vector<RelayView>& relays) {
  json::Value out = json::Value::array();
  if (!core.files()) return out;
  Files& files = *core.files();
  for (const RelayView& r : relays) {
    std::string folder = save_folder(core, r);
    json::Value e = json::Value::object();
    e.set("version", text(r.version));
    e.set("folder", text(folder));
    e.set("inside", json::Value::boolean(inside_home(core, folder)));
    json::Value list = json::Value::array();
    for (const std::string& n : listing(files, folder)) {
      FileStat st;
      OsError err;
      if (!files.stat(files.join(folder, n), &st, &err)) continue;
      if ((st.mode & 0170000) == 0040000) continue;
      json::Value f = json::Value::object();
      f.set("file", text(n));
      f.set("bytes", json::Value::uinteger(st.size));
      f.set("modified", json::Value::number(static_cast<double>(st.mtime_ns) / 1e9));
      list.push(f);
    }
    e.set("files", list);
    out.push(e);
  }
  return out;
}

std::string save_file(Core& core, const std::vector<RelayView>& relays, const std::string& version,
                      const std::string& file) {
  if (!core.files() || !save_file_name_ok(file)) return std::string();
  for (const RelayView& r : relays) {
    if (r.version != version) continue;
    std::string folder = save_folder(core, r);
    std::vector<std::string> names = listing(*core.files(), folder);
    if (std::find(names.begin(), names.end(), file) == names.end()) return std::string();
    return core.files()->join(folder, file);
  }
  return std::string();
}

std::string save_view(const std::string& data, bool* formatted) {
  std::optional<json::Value> v = json::loads_utf8(data);
  if (!v) {
    *formatted = false;
    return data;
  }
  *formatted = true;
  return unescape_non_ascii(json::dumps(*v, 2));
}

std::string valid_utf8(const std::string& s, size_t limit) {
  std::string out;
  size_t n = s.size() < limit ? s.size() : limit;
  out.reserve(n);
  for (size_t i = 0; i < n;) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    bool ok = len > 0 && i + len <= n;
    for (size_t k = 1; ok && k < len; ++k) ok = (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
    if (ok && len > 1) {
      uint32_t cp = c & (0x7F >> len);
      for (size_t k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
      ok = (len == 2 && cp >= 0x80) || (len == 3 && cp >= 0x800 && (cp < 0xD800 || cp > 0xDFFF)) ||
           (len == 4 && cp >= 0x10000 && cp <= 0x10FFFF);
    }
    if (!ok) {
      out += "\xEF\xBF\xBD";
      ++i;
      continue;
    }
    out.append(s, i, len);
    i += len;
  }
  return out;
}

std::string unescape_non_ascii(const std::string& s) {
  auto hex4 = [&](size_t at, uint32_t* out) {
    if (at + 4 > s.size()) return false;
    uint32_t v = 0;
    for (size_t i = at; i < at + 4; ++i) {
      char c = s[i];
      int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                                                                           : -1;
      if (d < 0) return false;
      v = v * 16 + static_cast<uint32_t>(d);
    }
    *out = v;
    return true;
  };
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] != '\\' || i + 1 >= s.size()) {
      out += s[i++];
      continue;
    }
    if (s[i + 1] != 'u') {
      out.append(s, i, 2);
      i += 2;
      continue;
    }
    uint32_t cp = 0;
    if (!hex4(i + 2, &cp) || cp < 0x80) {
      out.append(s, i, 2);
      i += 2;
      continue;
    }
    if (cp >= 0xD800 && cp < 0xDC00) {
      uint32_t low = 0;
      if (i + 12 <= s.size() && s[i + 6] == '\\' && s[i + 7] == 'u' && hex4(i + 8, &low) && low >= 0xDC00 &&
          low < 0xE000) {
        py::append_utf8(out, 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00));
        i += 12;
        continue;
      }
      out.append(s, i, 6);
      i += 6;
      continue;
    }
    if (cp >= 0xDC00 && cp < 0xE000) {
      out.append(s, i, 6);
      i += 6;
      continue;
    }
    py::append_utf8(out, cp);
    i += 6;
  }
  return out;
}

}
