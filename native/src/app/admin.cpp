#include "app/admin.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <string>

#include "app/system.h"
#include "app/test_hooks.h"
#include "core/accounts.h"
#include "core/config.h"
#include "core/core.h"
#include "core/env.h"
#include "core/json.h"
#include "core/ubi.h"
#include "core/util.h"
#include "platform/platform.h"
#include "server/console_log.h"
#include "server/http_server.h"
#include "server/startup.h"
#include "mbedtls/md.h"

namespace tcr::app {

namespace {

#include "app/admin_page.inc"
#include "app/admin_login.inc"

constexpr char kTokenMark[] = "{{TOKEN}}";
constexpr char kRunMark[] = "{{RUN}}";
constexpr char kAuthMark[] = "{{AUTH}}";
constexpr char kErrorMark[] = "{{ERROR}}";
constexpr char kCookie[] = "tcr_admin";
constexpr double kMaxLinkMinutes = 7 * 24 * 60;
constexpr size_t kMaxSessions = 64;
constexpr size_t kMaxFailAddresses = 4096;

struct State {
  Core* core = nullptr;
  AdminOptions o;
  std::string token;
  std::string run;
  int port = 0;
  std::string password;
  std::deque<std::string> sessions;
  std::map<std::string, std::deque<double>> fails;
};
State* g_state = nullptr;

bool desktop() {
#if defined(TCR_TEST_HOOKS)
  std::string forced = plat::env_var("TCREBORN_TEST_DESKTOP");
  if (forced == "0" || forced == "1") return forced == "1";
#endif
  return plat::desktop_session();
}

bool open_later(const std::string& target, const std::string& what) {
  bool started = plat::start_thread([target, what]() {
    plat::Error err;
    if (!plat::open_with_desktop(target, &err)) say("admin page: cannot open " + what + ": " + err.text);
  });
  if (!started) say("admin page: cannot open " + what + ": the system refused a thread");
  return started;
}

std::string lower_ascii(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos) return std::string();
  size_t b = s.find_last_not_of(" \t");
  return s.substr(a, b - a + 1);
}

bool same_secret(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

void common_headers(server::Response* r) {
  r->headers.push_back({"Cache-Control", "no-store"});
  r->headers.push_back({"X-Content-Type-Options", "nosniff"});
  r->headers.push_back({"X-Frame-Options", "DENY"});
  r->headers.push_back({"Referrer-Policy", "no-referrer"});
}

server::Response json_reply(int status, const json::Value& v) {
  server::Response r;
  r.status = status;
  r.payload = json::dumps(v);
  r.content_type = "application/json; charset=utf-8";
  common_headers(&r);
  return r;
}

server::Response error_reply(int status, const std::string& text) {
  json::Value v = json::Value::object();
  v.set("error", json::Value::string(text));
  return json_reply(status, v);
}

server::Response ok_reply(const std::string& text) {
  json::Value v = json::Value::object();
  v.set("ok", json::Value::boolean(true));
  v.set("text", json::Value::string(text));
  return json_reply(200, v);
}

bool host_ok(const server::Request& req, int port, const std::string& local_ip) {
  std::optional<std::string> host = req.headers.get("Host");
  if (!host) return false;
  std::string h = lower_ascii(trim(*host));
  for (const std::string& name : {std::string("127.0.0.1"), std::string("localhost"), local_ip}) {
    if (name.empty()) continue;
    if (h == name + ":" + std::to_string(port) || (port == 80 && h == name)) return true;
  }
  return false;
}

bool origin_ok(const server::Request& req, int port, const std::string& local_ip) {
  std::optional<std::string> origin = req.headers.get("Origin");
  if (!origin) return true;
  std::string o = lower_ascii(trim(*origin));
  std::string p = port == 80 ? std::string() : ":" + std::to_string(port);
  for (const std::string& name : {std::string("127.0.0.1"), std::string("localhost"), local_ip}) {
    if (!name.empty() && o == "http://" + name + p) return true;
  }
  return false;
}

std::string digest(const std::string& text) {
  unsigned char out[32];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const unsigned char*>(text.data()),
             text.size(), out);
  return std::string(reinterpret_cast<const char*>(out), sizeof(out));
}

std::string cookie(const server::Request& req, const std::string& name) {
  std::optional<std::string> header = req.headers.get("Cookie");
  if (!header) return std::string();
  size_t at = 0;
  while (at < header->size()) {
    size_t end = header->find(';', at);
    if (end == std::string::npos) end = header->size();
    std::string part = trim(header->substr(at, end - at));
    size_t eq = part.find('=');
    if (eq != std::string::npos && part.substr(0, eq) == name) return part.substr(eq + 1);
    at = end + 1;
  }
  return std::string();
}

bool logged_in(const server::Request& req) {
  std::string value = cookie(req, kCookie);
  if (value.empty()) return false;
  bool found = false;
  for (const std::string& sess : g_state->sessions) found |= same_secret(sess, value);
  return found;
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string percent_decode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      out += ' ';
    } else if (s[i] == '%' && i + 2 < s.size() && hex_digit(s[i + 1]) >= 0 && hex_digit(s[i + 2]) >= 0) {
      out += static_cast<char>(hex_digit(s[i + 1]) * 16 + hex_digit(s[i + 2]));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string query_value(const std::string& query, const std::string& key) {
  size_t at = 0;
  while (at <= query.size()) {
    size_t end = query.find('&', at);
    if (end == std::string::npos) end = query.size();
    std::string part = query.substr(at, end - at);
    size_t eq = part.find('=');
    if (percent_decode(part.substr(0, eq)) == key) {
      return eq == std::string::npos ? std::string() : percent_decode(part.substr(eq + 1));
    }
    at = end + 1;
  }
  return std::string();
}

void say_action(Core& core, const std::string& text) {
  core.env().print_line(stamped(core.env(), "admin page: " + text));
}

void fill(std::string* html, const char* mark, size_t mark_size, const std::string& value) {
  size_t at = html->find(mark);
  if (at != std::string::npos) html->replace(at, mark_size - 1, value);
}

server::Response html_reply(int status, std::string html, const char* form_action) {
  server::Response r;
  r.status = status;
  r.payload = std::move(html);
  r.content_type = "text/html; charset=utf-8";
  common_headers(&r);
  r.headers.push_back({"Content-Security-Policy",
                       std::string("default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                                   "connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action ") +
                           form_action + "; frame-ancestors 'none'"});
  return r;
}

server::Response page() {
  std::string html(reinterpret_cast<const char*>(kAdminPage), sizeof(kAdminPage));
  fill(&html, kTokenMark, sizeof(kTokenMark), g_state->token);
  fill(&html, kRunMark, sizeof(kRunMark), g_state->run);
  fill(&html, kAuthMark, sizeof(kAuthMark), g_state->password.empty() ? "0" : "1");
  return html_reply(200, std::move(html), "'none'");
}

server::Response login_page(int status, const std::string& error) {
  std::string html(reinterpret_cast<const char*>(kAdminLogin), sizeof(kAdminLogin));
  fill(&html, kErrorMark, sizeof(kErrorMark), error);
  server::Response r = html_reply(status, std::move(html), "'self'");
  for (auto& h : r.headers) {
    if (h.first == "Referrer-Policy") h.second = "same-origin";
  }
  return r;
}

server::Response login(const server::Request& req, const std::string& client_ip) {
  State& s = *g_state;
  const double now = plat::monotonic();
  std::deque<double>& mine = s.fails[client_ip];
  while (!mine.empty() && now - mine.front() >= ubi::kFailWindow) mine.pop_front();
  if (mine.size() >= static_cast<size_t>(ubi::kFailLimit)) return login_page(429, "wait");
  std::string given;
  std::optional<std::string> kind = req.headers.get("Content-Type");
  if (kind && lower_ascii(*kind).find("application/json") != std::string::npos) {
    std::optional<json::Value> v =
        json::loads_bytes(reinterpret_cast<const uint8_t*>(req.raw.data()), req.raw.size(), json::kHttpMaxDepth);
    const json::Value* pw = v && v->is_object() ? v->get("password") : nullptr;
    if (pw && pw->is_string()) given = pw->as_string();
  } else {
    given = query_value(req.raw, "password");
  }
  if (!same_secret(digest(given), digest(s.password))) {
    mine.push_back(now);
    if (s.fails.size() > kMaxFailAddresses) {
      for (auto it = s.fails.begin(); it != s.fails.end();) {
        it = !it->second.empty() && now - it->second.back() < ubi::kFailWindow ? std::next(it) : s.fails.erase(it);
      }
    }
    say_action(*s.core, "wrong password from " + client_ip);
    return login_page(401, "wrong");
  }
  s.fails.erase(client_ip);
  uint8_t raw[32];
  if (!plat::random_bytes(raw, sizeof(raw))) return login_page(500, "wrong");
  std::string id = py::hex(raw, sizeof(raw));
  s.sessions.push_back(id);
  while (s.sessions.size() > kMaxSessions) s.sessions.pop_front();
  server::Response r = html_reply(303, std::string(), "'none'");
  r.headers.push_back({"Location", "/"});
  r.headers.push_back({"Set-Cookie", std::string(kCookie) + "=" + id + "; Path=/; HttpOnly; SameSite=Strict"});
  return r;
}

server::Response logout(const server::Request& req) {
  std::string value = cookie(req, kCookie);
  auto& sessions = g_state->sessions;
  for (auto it = sessions.begin(); it != sessions.end();) it = *it == value ? sessions.erase(it) : std::next(it);
  server::Response r = ok_reply("logged out");
  r.headers.push_back({"Set-Cookie", std::string(kCookie) + "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict"});
  return r;
}

server::Response refused() {
  server::Response r;
  r.status = 403;
  r.content_type = "text/plain; charset=utf-8";
  common_headers(&r);
  return r;
}

server::Response log_view(const std::string& query) {
  uint64_t after = 0;
  std::string a = query_value(query, "after");
  for (char c : a) {
    if (c < '0' || c > '9' || after > UINT64_MAX / 10 - 10) {
      after = 0;
      break;
    }
    after = after * 10 + static_cast<uint64_t>(c - '0');
  }
  uint64_t last = 0;
  std::vector<server::console_log::Line> lines = server::console_log::since(after, &last);
  json::Value list = json::Value::array();
  for (const auto& l : lines) {
    json::Value e = json::Value::object();
    e.set("n", json::Value::uinteger(l.number));
    e.set("t", json::Value::number(l.time));
    e.set("error", json::Value::boolean(l.error));
    e.set("text", json::Value::string(l.text));
    list.push(e);
  }
  json::Value out = json::Value::object();
  out.set("run", json::Value::string(g_state->run));
  out.set("last", json::Value::uinteger(last));
  out.set("lines", list);
  return json_reply(200, out);
}

server::Response save_view(Core& core, const std::string& query, bool download) {
  std::string version = query_value(query, "version");
  std::string file = query_value(query, "file");
  std::string path = tcr::admin::save_file(core, g_state->o.relays, version, file);
  if (path.empty()) return error_reply(404, "no such save");
  std::string data;
  OsError err;
  if (!core.files()->read(path, &data, &err)) return error_reply(500, "cannot read " + path + ": " + err.text());
  if (download) {
    server::Response r;
    r.status = 200;
    r.payload = data;
    r.content_type = "application/octet-stream";
    common_headers(&r);
    r.headers.push_back({"Content-Disposition", "attachment; filename=\"" + file + "\""});
    return r;
  }
  bool formatted = false;
  std::string text = tcr::admin::save_view(data, &formatted);
  json::Value out = json::Value::object();
  out.set("version", json::Value::string(version));
  out.set("file", json::Value::string(file));
  out.set("formatted", json::Value::boolean(formatted));
  if (!formatted) text = tcr::admin::valid_utf8(text);
  out.set("text", json::Value::string(text));
  return json_reply(200, out);
}

json::Value body_of(const server::Request& req) {
  std::optional<json::Value> v =
      json::loads_bytes(reinterpret_cast<const uint8_t*>(req.raw.data()), req.raw.size(), json::kHttpMaxDepth);
  if (v && v->is_object()) return *v;
  return json::Value::object();
}

std::string text_field(const json::Value& body, const char* key) {
  const json::Value* v = body.get(key);
  return v && v->is_string() ? v->as_string() : std::string();
}

server::Response link(Core& core, const json::Value& body) {
  std::string name = text_field(body, "name");
  if (name.empty()) return error_reply(400, "no name");
  double minutes = kLinkMinutes;
  if (const json::Value* m = body.get("minutes")) {
    if (m->is_float()) {
      minutes = m->as_float();
    } else if (m->is_int() && m->as_i64()) {
      minutes = static_cast<double>(*m->as_i64());
    } else {
      minutes = -1;
    }
  }
  if (!(0 < minutes && minutes <= kMaxLinkMinutes)) {
    return error_reply(400, "minutes must be more than 0 and at most a week");
  }
  AccountStore& store = core.ubi().accounts();
  Raised raised;
  std::optional<std::string> display = store.display_name(name, &raised);
  if (!raised.type.empty()) return error_reply(500, raised.line());
  if (!display) return error_reply(404, "no claim for " + name);
  std::optional<bool> opened = store.open_link(name, minutes, &raised);
  if (!opened) return error_reply(500, raised.line());
  if (!*opened) return error_reply(500, "could not write " + store.path());
  display = store.display_name(name, &raised);
  char g[64];
  std::snprintf(g, sizeof(g), "%g", minutes);
  std::string text = "link window for " + display.value_or(name) + " open for " + g +
                     " minutes: log in with the new device now";
  say_action(core, text);
  json::Value v = json::Value::object();
  v.set("ok", json::Value::boolean(true));
  v.set("text", json::Value::string(text));
  v.set("name", json::Value::string(display.value_or(name)));
  v.set("minutes", json::Value::number(minutes));
  return json_reply(200, v);
}

server::Response free_name(Core& core, const json::Value& body) {
  std::string name = text_field(body, "name");
  if (name.empty()) return error_reply(400, "no name");
  Raised raised;
  bool freed = core.ubi().accounts().free(name, &raised);
  if (!raised.type.empty()) return error_reply(500, raised.line());
  if (!freed) return error_reply(404, "no claim for " + name);
  say_action(core, "freed " + name);
  return ok_reply("freed " + name);
}

server::Response open_folder(Core& core, const json::Value& body) {
  std::string version = text_field(body, "version");
  std::string folder;
  for (const tcr::admin::RelayView& r : g_state->o.relays) {
    if (r.version == version) folder = tcr::admin::save_folder(core, r);
  }
  if (folder.empty()) return error_reply(404, "no save folder for this game version");
  if (!tcr::admin::inside_home(core, folder)) return error_reply(403, "the save folder is outside the server folder");
  if (!plat::is_dir(folder)) folder = plat::dir_name(folder);
  if (!plat::is_dir(folder) || !tcr::admin::inside_home(core, folder)) {
    return error_reply(404, "the save folder does not exist yet");
  }
  json::Value out = json::Value::object();
  out.set("folder", json::Value::string(folder));
  if (!desktop()) {
    out.set("error", json::Value::string("no desktop session on the server's computer"));
    return json_reply(409, out);
  }
  if (!g_state->o.test_mode && !open_later(folder, "the folder")) {
    out.set("error", json::Value::string("cannot open the folder: the system refused a thread"));
    return json_reply(500, out);
  }
  out.set("ok", json::Value::boolean(true));
  return json_reply(200, out);
}

server::Response route(const server::Request& req) {
  State& s = *g_state;
  Core& core = *s.core;
  std::string client_ip = req.client_ip, local_ip = req.host;
  int client_port = req.client_port;
  if (s.o.hooks) s.o.hooks->http_client(req.headers, &client_ip, &client_port, &local_ip);
  if (!ubi::trusted(client_ip)) return refused();
  if (!host_ok(req, s.port, local_ip)) {
    return error_reply(421, "this page answers only as localhost, 127.0.0.1 or this computer's address");
  }
  std::string path = req.path, query;
  size_t q = path.find('?');
  if (q != std::string::npos) {
    query = path.substr(q + 1);
    path = path.substr(0, q);
  }
  if (!s.password.empty() && !logged_in(req)) {
    if (req.method == "POST" && path == "/login") {
      if (!origin_ok(req, s.port, local_ip)) return error_reply(403, "this form belongs to another page");
      return login(req, client_ip);
    }
    if (req.method == "GET" && (path == "/" || path == "/index.html")) return login_page(200, "");
    json::Value v = json::Value::object();
    v.set("error", json::Value::string("log in first"));
    v.set("login", json::Value::boolean(true));
    return json_reply(401, v);
  }
  if (req.method == "GET") {
    if (path == "/" || path == "/index.html") return page();
    if (path == "/api/facts") {
      tcr::admin::Facts f = s.o.facts;
      f.uptime = plat::monotonic() - f.started_mono;
      return json_reply(200, tcr::admin::facts(core, f, s.o.relays));
    }
    if (path == "/api/clients") return json_reply(200, tcr::admin::clients(core, s.o.relays));
    if (path == "/api/names") return json_reply(200, tcr::admin::names(core));
    if (path == "/api/saves") return json_reply(200, tcr::admin::saves(core, s.o.relays));
    if (path == "/api/log") return log_view(query);
    if (path == "/api/save") return save_view(core, query, false);
    if (path == "/api/save/download") return save_view(core, query, true);
    return error_reply(404, "not found");
  }
  if (req.method != "POST") return error_reply(405, "method not allowed");
  std::optional<std::string> token = req.headers.get("X-Admin-Token");
  if (!token || !same_secret(trim(*token), s.token) || !origin_ok(req, s.port, local_ip)) {
    json::Value v = json::Value::object();
    v.set("error", json::Value::string("this action needs the page's token"));
    v.set("stale_token", json::Value::boolean(true));
    return json_reply(403, v);
  }
  json::Value body = body_of(req);
  if (path == "/api/link") return link(core, body);
  if (path == "/api/free") return free_name(core, body);
  if (path == "/api/open-folder") return open_folder(core, body);
  if (path == "/api/logout") return logout(req);
  if (path == "/api/stop") {
    say_action(core, "stop");
    request_stop();
    return ok_reply("stopping");
  }
  return error_reply(404, "not found");
}

}

void start_admin_page(Core& core, AdminOptions options) {
  config::Result<std::optional<bool>> enabled = core.config().ui_enabled();
  config::Result<bool> open = core.config().ui_open();
  config::Result<std::string> port_text = core.config().ui_port();
  config::Result<std::string> password = core.config().ui_password();
  config::Result<bool> link_home = core.config().link_home();
  config::Result<std::optional<std::string>> advertised = core.config().advertised_ip();
  auto off = [](const std::string& line) {
    if (!line.empty()) say(line);
    server::console_log::stop();
  };
  for (const config::Error* e : {enabled.ok() ? nullptr : &enabled.error, open.ok() ? nullptr : &open.error,
                                 port_text.ok() ? nullptr : &port_text.error,
                                 password.ok() ? nullptr : &password.error}) {
    if (e) return off("Admin page: off (config.ini [ui]: " + e->type + ": " + e->message + ")");
  }
  if (*enabled.value && !**enabled.value) return off("");
  std::optional<int64_t> port = py::int_text_to_i64(*port_text.value);
  if (!port || *port < 0 || *port > 65535) {
    return off("Admin page: off (config.ini [ui] port " + *port_text.value + " is not a port from 0 to 65535)");
  }
  std::string local_only;
  if (link_home.ok() && !*link_home.value) {
    local_only = "link_home = off";
  } else if (advertised.ok() && *advertised.value && !ubi::trusted(**advertised.value)) {
    local_only = "[server] ip is a public address";
  }
  uint8_t raw[40];
  if (!plat::random_bytes(raw, sizeof(raw))) return off("Admin page: off (the system's random source failed)");
  g_state = new State;
  g_state->core = &core;
  g_state->o = std::move(options);
  g_state->token = py::hex(raw, 32);
  g_state->run = py::hex(raw + 32, 8);
  g_state->password = *password.value;
  server::Hooks hooks;
  hooks.route = route;
  server::HttpServer& http = *new server::HttpServer(hooks);
  const std::string bind_ip = local_only.empty() ? "0.0.0.0" : "127.0.0.1";
  int want = static_cast<int>(*port);
  plat::Error err;
  bool ok = http.start(bind_ip, want, nullptr, "", &err);
  if (!ok && server::fallback_port(want)) {
    int alt = server::fallback_port(want);
    say("admin page: tcp port " + std::to_string(want) + " " +
        (plat::is_addr_in_use(err) ? "is in use by another program" : "needs more rights") + "; using " +
        std::to_string(alt));
    want = alt;
    ok = http.start(bind_ip, want, nullptr, "", &err);
  }
  if (!ok) return off("Admin page: off (cannot open tcp port " + std::to_string(want) + ": " + err.text + ")");
  g_state->port = http.port();
  g_state->o.facts.admin_port = g_state->port;
  const std::string pw = g_state->password.empty() ? "" : ", password";
  const std::string local_url = "http://127.0.0.1:" + std::to_string(g_state->port) + "/";
  if (local_only.empty()) {
    std::string ip = advertised.ok() && *advertised.value ? **advertised.value : g_state->o.facts.lan_ip;
    say("Admin page: http://" + ip + ":" + std::to_string(g_state->port) + "/ (home network" + pw + ")");
  } else {
    say("Admin page: " + local_url + " (only on this computer: " + local_only + pw + ")");
  }
  if (*open.value && !g_state->o.test_mode && desktop()) open_later(local_url, "the browser");
}

}
