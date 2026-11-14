#include "app/server.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "app/admin.h"
#include "app/args.h"
#include "app/system.h"
#include "app/test_hooks.h"
#include "core/accounts.h"
#include "core/config.h"
#include "core/core.h"
#include "core/discovery.h"
#include "core/json.h"
#include "core/ubi.h"
#include "core/util.h"
#include "core/v1_00/relay.h"
#include "core/v1_21/factions.h"
#include "core/v1_21/relay.h"
#include "core/version.h"
#include "platform/files.h"
#include "platform/platform.h"
#include "server/capture.h"
#include "server/console_log.h"
#include "server/core_lock.h"
#include "server/http_server.h"
#include "server/startup.h"
#include "server/tls.h"
#include "server/udp_loop.h"

namespace tcr::app {

namespace {

const char kUsage[] =
    "usage: TheCrewReborn-Server [-h] [--version] [--ip IP] [--port PORT]\n"
    "                            [--http-port HTTP_PORT] [--log LOG] [--save SAVE]\n"
    "                            [--verbose]\n";

const char kHelp[] =
    "usage: TheCrewReborn-Server [-h] [--version] [--ip IP] [--port PORT]\n"
    "                            [--http-port HTTP_PORT] [--log LOG] [--save SAVE]\n"
    "                            [--verbose]\n"
    "\n"
    "options:\n"
    "  -h, --help            show this help message and exit\n"
    "  --version             show program's version number and exit\n"
    "  --ip IP\n"
    "  --port PORT\n"
    "  --http-port HTTP_PORT\n"
    "  --log LOG\n"
    "  --save SAVE           player save file (JSON)\n"
    "  --verbose, -v         show ports and settings at the start\n";

const ParserSpec& server_parser() {
  static const ParserSpec spec = [] {
    ParserSpec p;
    p.prog = "TheCrewReborn-Server";
    p.usage = kUsage;
    p.help = kHelp;
    p.version = std::string("The Crew Reborn server ") + kVersion;
    p.options = {
        {{"-h", "--help"}, "help", OptionSpec::kHelp, OptionSpec::kText},
        {{"--version"}, "version", OptionSpec::kVersion, OptionSpec::kText},
        {{"--ip"}, "ip", OptionSpec::kValue, OptionSpec::kText},
        {{"--port"}, "port", OptionSpec::kValue, OptionSpec::kInt},
        {{"--http-port"}, "http_port", OptionSpec::kValue, OptionSpec::kInt},
        {{"--log"}, "log", OptionSpec::kValue, OptionSpec::kText},
        {{"--save"}, "save", OptionSpec::kValue, OptionSpec::kText},
        {{"--verbose", "-v"}, "verbose", OptionSpec::kFlag, OptionSpec::kText},
    };
    return p;
  }();
  return spec;
}

struct GameVersion {
  const char* name;
  int port_offset;
};
const GameVersion kVersions[] = {{"1.21", 0}, {"1.00", v1_00::PORT_OFFSET}};

int exit_with(const std::string& text) {
  say_error(text);
  return 1;
}

int crash(const std::string& line) {
  say_error("Traceback (most recent call last):");
  say_error(line);
  return 1;
}

int crash(const config::Error& e) {
  return crash((e.configparser() ? "configparser." : "") + e.type + ": " + e.message);
}

int crash(const OsError& e) { return crash(std::string(e.type()) + ": " + e.text()); }

OsError os_error(const plat::Error& e, const std::string& path, const std::string* path2 = nullptr) {
  OsError out;
  out.code = e.code;
  out.winerror = e.winerror;
  out.message = e.text;
  out.filename = path;
  if (path2) out.filename2 = *path2;
  return out;
}

int crash(const plat::Error& e) {
  return crash(std::string(plat::python_error_name(e)) + ": " + plat::python_error_text(e));
}

template <class T>
bool setting(const config::Result<T>& r, T* out, int* code) {
  if (!r.ok()) {
    *code = crash(r.error);
    return false;
  }
  *out = *r.value;
  return true;
}

bool print_lines(const config::Outcome& o, const std::vector<std::string>& lines, int* code) {
  for (const std::string& line : lines) say(line);
  if (o.kind == config::Outcome::kExit) {
    *code = exit_with(o.text);
    return false;
  }
  if (o.kind == config::Outcome::kCrash) {
    config::Error e;
    e.type = o.text.substr(0, o.text.find(':'));
    *code = crash((e.configparser() ? "configparser." : "") + o.text);
    return false;
  }
  return true;
}

bool copy_tree(Files& files, const std::string& src, const std::string& dst, OsError* err) {
  plat::Error e;
  if (!plat::is_dir(src)) {
    std::string data;
    if (!plat::read_file(src, &data, &e)) {
      *err = os_error(e, src);
      return false;
    }
    if (!plat::write_file(dst, data.data(), data.size(), &e)) {
      *err = os_error(e, dst);
      return false;
    }
    return true;
  }
  if (!files.makedirs(dst, err)) return false;
  std::vector<std::string> names;
  if (!files.listdir(src, &names, err)) return false;
  for (const std::string& n : names) {
    if (!copy_tree(files, plat::join_path(src, n), plat::join_path(dst, n), err)) return false;
  }
  return true;
}

bool move_into_version_folders(Files& files, const std::string& root, int* code) {
  std::string target = plat::join_path(root, "1.21");
  std::vector<std::string> old;
  for (const char* n : {"players", "player.json", "factions.json"}) {
    if (plat::path_exists(plat::join_path(root, n)) && !plat::path_exists(plat::join_path(target, n))) old.push_back(n);
  }
  if (old.empty()) return true;
  plat::LocalTime t = plat::local_now();
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d", t.year, t.month, t.day, t.hour, t.minute,
                t.second);
  std::string backup = plat::join_path(plat::join_path(root, "backup"), std::string("before-version-folders-") + stamp);
  OsError err;
  if (files.lexists(backup)) {
#if defined(_WIN32)
    *code = crash(os_error(plat::system_error(183), backup));
#else
    *code = crash(os_error(plat::system_error(EEXIST), backup));
#endif
    return false;
  }
  if (!files.makedirs(backup, &err)) {
    *code = crash(err);
    return false;
  }
  for (const std::string& n : old) {
    if (!copy_tree(files, plat::join_path(root, n), plat::join_path(backup, n), &err)) {
      *code = crash(err);
      return false;
    }
  }
  if (!files.makedirs(target, &err)) {
    *code = crash(err);
    return false;
  }
  for (const std::string& n : old) {
    if (!files.rename(plat::join_path(root, n), plat::join_path(target, n), &err)) {
      *code = crash(err);
      return false;
    }
  }
  say("Moved the saves into " + target + " (copy in " + backup + ")");
  return true;
}

class LoopSender final : public discovery::Sender {
 public:
  LoopSender(server::UdpLoop& loop, const plat::Addr& to) : loop_(loop), to_(to) {}
  bool send(const Bytes& reply) override {
    plat::Error err;
    return loop_.send(server::Datagram{std::string(reply.begin(), reply.end()), to_}, &err);
  }

 private:
  server::UdpLoop& loop_;
  plat::Addr to_;
};

struct RelayPort {
  std::string name;
  std::unique_ptr<v1_21::Relay> relay;
  std::string ip;
  int port = 0;
  server::UdpLoop loop;
};

std::string server_address(const std::string& ip, int https_port) {
  return https_port && https_port != 443 ? ip + ":" + std::to_string(https_port) : ip;
}

constexpr int64_t kHugePort = int64_t(1) << 40;

int64_t port_number(const std::string& text) {
  std::optional<std::string> canon = py::parse_int(text);
  if (!canon) return 0;
  std::optional<int64_t> v = py::int_text_to_i64(*canon);
  if (!v) return (*canon)[0] == '-' ? -kHugePort : kHugePort;
  if (*v > kHugePort) return kHugePort;
  if (*v < -kHugePort) return -kHugePort;
  return *v;
}

bool port_ok(int64_t port) { return port >= 0 && port <= 65535; }

const char kOverflow[] = "OverflowError: bind(): port must be 0-65535.";

std::atomic<bool> g_serving{false};
std::atomic<double> g_stop_at{0.0};

bool stop_due() {
  double at = g_stop_at.load();
  return at > 0 && plat::monotonic() >= at;
}

void watch_interrupt() {
  plat::start_thread([]() {
    while (!plat::interrupted() && !stop_due()) plat::sleep_seconds(0.05);
    say("\nServer stopped.");
    if (g_serving.load()) {
      server::CoreLock lock;
      std::_Exit(0);
    }
    std::_Exit(0);
  });
}

}

void request_stop() { g_stop_at.store(plat::monotonic() + 0.3); }

int run_server(const std::vector<std::string>& argv) {
  const double started_wall = plat::wall_time(), started_mono = plat::monotonic();
  server::console_log::start();
  plat::stream_errors_replace();
  plat::catch_interrupt();
  watch_interrupt();
  const std::string home = home_folder();
  const std::string config_path = config_file(home);

  platform::SystemFiles& files = *new platform::SystemFiles;
  SystemEnv& env = *new SystemEnv(files, config_path);
  Core& core = *new Core(env, home, "");
  core.set_files(&files);
  v1_21::factions::path(core);

  {
    std::string target = first_config_file(home);
    std::string example = plat::join_path(plat::dir_name(plat::executable_path()), "config.example.ini");
    if (!plat::path_exists(target) && plat::path_exists(example)) {
      std::string data;
      plat::Error err;
      if (!plat::read_file(example, &data, &err)) return crash(os_error(err, example));
      if (!plat::write_file(target, data.data(), data.size(), &err)) return crash(os_error(err, target));
      say("created " + target);
    }
  }

  std::unique_ptr<server::CoreLock> starting(new server::CoreLock);
  int code = 0;

  if (!print_lines(config::check_config(core.config()), {}, &code)) return code;

  std::pair<std::string, std::string> relay_address;
  if (!setting(core.config().relay_address(), &relay_address, &code)) return code;
  std::string http_port_setting;
  if (!setting(core.config().http_port(), &http_port_setting, &code)) return code;
  std::string default_save;
  if (!setting(core.config().save_path(), &default_save, &code)) return code;
  std::optional<Parsed> args = parse_args(server_parser(), argv, &code);
  if (!args) return code;
  const std::string ip = args->get("ip", relay_address.first);
  const int64_t port = port_number(args->get("port", relay_address.second));
  const int64_t http_port = port_number(args->get("http_port", http_port_setting));
  const std::string log = args->get("log", plat::join_path(plat::join_path(home, "captures"), "proxy.jsonl"));
  const std::string save = args->get("save", default_save);
  const bool verbose = args->flags.count("verbose") != 0;

  say(std::string("The Crew Reborn server ") + kVersion);
  TestHooks* hooks = TestHooks::from_environment(core, env);
  {
    std::vector<std::string> lines;
    if (!print_lines(config::say_settings(core.config(), &lines), lines, &code)) return code;
  }
  start_pause();

  const std::string save_root = plat::join_path(home, "save");
  {
    std::string folder = plat::join_path(save_root, "1.00"), old = plat::join_path(save_root, "v100");
    OsError err;
    if (!plat::path_exists(folder) && plat::is_dir(old) && !files.rename(old, folder, &err)) return crash(err);
    if (!files.makedirs(folder, &err)) return crash(err);
  }

  {
    std::set<int64_t> relay_ports;
    for (const GameVersion& g : kVersions) relay_ports.insert(port + g.port_offset);
    std::vector<int64_t> probe(relay_ports.begin(), relay_ports.end());
    probe.push_back(discovery_port());
    for (int64_t p : probe) {
      if (!port_ok(p)) return crash(kOverflow);
      if (!plat::udp_port_free(static_cast<int>(p))) {
        return exit_with("\nThe Crew Reborn server is already running on this computer (udp port " +
                         std::to_string(p) + " is in use).\nUse that one, or close its window first.");
      }
    }
  }

  ubi::Ubi& ubi = core.ubi();
  {
    bool damaged = false;
    std::string error;
    if (!ubi.server_id().get(&damaged, &error)) {
      if (damaged) return exit_with("ERROR: " + error);
      return exit_with("ERROR: the server id " + ubi.server_id().path() + " cannot be written: " + error +
                       ". The server does not start.");
    }
  }
  const std::string log_dir = plat::dir_name(log);
  {
    OsError err;
    if (!files.makedirs(log_dir, &err)) return crash(err);
  }
  {
    bool configured = false;
    if (!setting(core.config().save_path_configured(), &configured, &code)) return code;
    std::string abs;
    OsError err;
    if (!configured && files.abspath(save, &abs, &err) && abs == default_save &&
        !move_into_version_folders(files, save_root, &code)) {
      return code;
    }
  }
  std::string abilities, unlocks, dlc;
  if (!setting(core.config().abilities(), &abilities, &code) || !setting(core.config().unlocks(), &unlocks, &code) ||
      !setting(core.config().dlc(), &dlc, &code)) {
    return code;
  }
  if (verbose) {
    say("player save: " + save);
    say("abilities: " + abilities + ", unlocks: " + unlocks + ", dlc: " + dlc);
  }
  {
    std::vector<std::string> lines;
    if (!print_lines(config::dlc_line(core.config(), &lines), lines, &code)) return code;
    lines.clear();
    if (!print_lines(config::check_server_ip(core.config(), &lines), lines, &code)) return code;
  }

  {
    Raised raised;
    json::Value entries = ubi.accounts().entries(&raised);
    if (!raised.type.empty()) return crash(raised.line());
    std::set<std::string> claimed = {account_key(kDefaultName)};
    for (const json::Member& m : entries.members()) {
      const json::Value* name = m.value.get("name");
      if (name && name->is_string()) claimed.insert(account_key(name->as_string()));
    }
    std::set<std::string> unclaimed;
    for (const GameVersion& g : kVersions) {
      std::string folder = plat::join_path(plat::join_path(save_root, g.name), "players");
      std::vector<std::string> names;
      plat::Error err;
      if (!plat::is_dir(folder)) continue;
      if (!plat::list_dir(folder, &names, &err)) return crash(os_error(err, folder));
      for (const std::string& f : names) {
        if (f.size() < 5 || f.compare(f.size() - 5, 5, ".json") != 0) continue;
        std::string stem = f.substr(0, f.size() - 5);
        if (save_name(stem) == stem && plat::is_file(plat::join_path(folder, f)) && !claimed.count(account_key(stem))) {
          unclaimed.insert(stem);
        }
      }
    }
    if (!unclaimed.empty()) {
      std::string list;
      for (const std::string& n : unclaimed) list += (list.empty() ? "" : ", ") + n;
      say("unclaimed saves (anyone may claim them at the next login): " + list);
    }
  }
  {
    std::vector<std::string> lines;
    if (!print_lines(config::warn_link_home(core.config(), &ubi::trusted, &lines), lines, &code)) return code;
    lines.clear();
    if (!print_lines(config::warn_multiplayer_public(core.config(), &ubi::trusted, &lines), lines, &code)) return code;
  }
  for (const GameVersion& g : kVersions) {
    std::string rel = plat::join_path(plat::join_path("save", g.name), "player.json");
    if (plat::is_file(plat::join_path(home, rel))) {
      say(rel + " is the old single-player save and is no longer used; to keep it, rename it to players/<Name>.json "
                "and log in as <Name>");
    }
  }

  const std::string requests_log = plat::join_path(log_dir, "requests.jsonl");
  server::Hooks http_hooks;
  http_hooks.route = [&core, hooks](const server::Request& req) {
    server::Response out;
    json::Value body = json::Value::object();
    if (!req.raw.empty()) {
      std::optional<json::Value> parsed =
          json::loads_bytes(reinterpret_cast<const uint8_t*>(req.raw.data()), req.raw.size(), json::kHttpMaxDepth);
      if (parsed && parsed->is_object()) body = std::move(*parsed);
    }
    std::string client_ip = req.client_ip;
    int client_port = req.client_port;
    std::string host = req.host;
    if (hooks) hooks->http_client(req.headers, &client_ip, &client_port, &host);
    ubi::Reply r = core.ubi().route(req.method, req.path, body, host, client_ip, client_port, req.auth, req.raw);
    if (r.raised) {
      out.raised = true;
      out.exception = r.exception.line();
      return out;
    }
    out.status = r.status;
    out.binary = r.binary;
    if (r.binary) {
      out.payload = r.bytes;
    } else {
      out.payload = json::dumps(r.json);
      out.log_reply = json::dumps(ubi::loggable(r.json));
    }
    return out;
  };
  http_hooks.say = [&core](const std::string& client_ip, const std::string& text, bool verified) {
    core.ubi().say(client_ip, text, verified);
  };
  http_hooks.live_ticket = [&core](const std::string& auth, std::string* raised) {
    Raised r;
    bool live = core.ubi().live_ticket(auth, &r);
    if (!r.type.empty()) *raised = r.line();
    return live;
  };
  server::HttpServer& http = *new server::HttpServer(http_hooks);
  int http_bound = 0;
  std::string message, crash_line;
  if (!port_ok(http_port)) return crash(kOverflow);
  if (!server::bind_with_fallback(
          "http", static_cast<int>(http_port),
          [&](int p, plat::Error* e) { return http.start("0.0.0.0", p, nullptr, requests_log, e); }, &http_bound,
          &message, &crash_line)) {
    return message.empty() ? crash(crash_line) : exit_with(message);
  }
  ubi.set_http_port(http_bound);
  std::string https_text;
  if (!setting(core.config().https_port(), &https_text, &code)) return code;
  const int64_t https_port = port_number(https_text);
  int https_bound = 0;
  std::unique_ptr<tls::ServerContext>& tls_context = *new std::unique_ptr<tls::ServerContext>;
  server::HttpServer& https = *new server::HttpServer(http_hooks);
  if (https_port) {
    std::pair<std::string, std::string> cert;
    std::optional<std::string> advertised;
    if (!setting(core.config().tls_cert(), &cert, &code) || !setting(core.config().advertised_ip(), &advertised, &code)) {
      return code;
    }
    if (!files.exists(cert.first) || !files.exists(cert.second)) {
      for (const std::string* path : {&cert.first, &cert.second}) {
        std::string abs;
        OsError err;
        if (!files.abspath(*path, &abs, &err) || !files.makedirs(plat::dir_name(abs), &err)) return crash(err);
      }
    }
    bool wrote = false;
    std::string why, failed_path;
    plat::Error failed;
    if (!tls::ensure(cert.first, cert.second, advertised.value_or(""), &wrote, &why, &failed, &failed_path)) {
      return failed_path.empty() ? crash("ValueError: " + why) : crash(os_error(failed, failed_path));
    }
    if (wrote) say("created the HTTPS certificate " + cert.first);
    std::string ssl_error;
    bool overflow = false;
    auto serve_tls = [&](int p, plat::Error* e) {
      tls_context.reset(new tls::ServerContext);
      std::string reason;
      plat::Error load_error;
      ssl_error.clear();
      if (!tls_context->load(cert.first, cert.second, &reason, &load_error)) {
        if (load_error.code == 0) {
          ssl_error = reason;
          *e = plat::Error();
          e->text = reason;
        } else {
          *e = load_error;
        }
        return false;
      }
      if (!port_ok(p)) {
        overflow = true;
        return false;
      }
      return https.start("0.0.0.0", p, tls_context.get(), requests_log, e);
    };
    if (https_port < INT32_MIN || https_port > INT32_MAX) return crash(kOverflow);
    if (!server::bind_with_fallback("https", static_cast<int>(https_port), serve_tls, &https_bound, &message,
                                    &crash_line)) {
      if (overflow) return crash(kOverflow);
      if (!message.empty()) return exit_with(message);
      return crash(ssl_error.empty() ? crash_line : "ssl.SSLError: " + ssl_error);
    }
    if (verbose) say("https on tcp 0.0.0.0:" + std::to_string(https_bound));
  }

  discovery::Settings settings;
  bool bots = false, afk = false;
  {
    std::optional<std::string> advertised;
    if (!setting(core.config().advertised_ip(), &advertised, &code) || !setting(core.config().bots(), &bots, &code) ||
        !setting(core.config().afk_kick(), &afk, &code)) {
      return code;
    }
    settings.relay_port = port;
    settings.http_port = http_bound;
    settings.advertise = advertised;
    settings.bots = bots;
    settings.afk = afk;
    settings.https_port = https_bound;
    settings.dlc = dlc == "all";
    bool damaged = false;
    std::string error;
    std::optional<Bytes> id = ubi.server_id().get(&damaged, &error);
    if (!id) return crash("OSError: " + error);
    settings.server_id = *id;
  }
  SystemRoute& route = *new SystemRoute;
  discovery::Discovery& finder = *new discovery::Discovery(env, settings, route);
  server::UdpLoop& discovery_loop = *new server::UdpLoop;
  {
    plat::Error err;
    if (!discovery_loop.open("0.0.0.0", discovery_port(), true, &err)) return crash(err);
  }
  discovery_loop.start(512, [&finder, &discovery_loop](const std::string& data, const plat::Addr& from) {
    LoopSender sender(discovery_loop, from);
    finder.datagram(Bytes(data.begin(), data.end()), from.ip, sender);
    return std::vector<server::Datagram>();
  }, nullptr);
  if (verbose) {
    say("http on tcp 0.0.0.0:" + std::to_string(http_bound) + ", discovery on udp 0.0.0.0:" +
        std::to_string(discovery_port()) + ", relay on udp " + ip + ":" + std::to_string(port) + ", freeride bots " +
        (bots ? "on" : "off") + ", idle kick " + (afk ? "on" : "off"));
  }

  SystemRelayHost& relay_host = *new SystemRelayHost;
  std::vector<std::unique_ptr<RelayPort>>& relays = *new std::vector<std::unique_ptr<RelayPort>>;
  for (const GameVersion& g : kVersions) {
    std::unique_ptr<RelayPort> rp(new RelayPort);
    rp->name = g.name;
    rp->ip = ip;
    rp->port = static_cast<int>(port + g.port_offset);
    v1_21::RelayOptions o;
    o.ip = ip;
    o.port = rp->port;
    if (g.port_offset == 0) {
      std::pair<Bytes, Bytes> proxy_key;
      if (!setting(core.config().proxy_key(), &proxy_key, &code)) return code;
      o.log_path = log;
      o.save_path = save;
      o.abilities = abilities;
      o.unlocks = unlocks;
      o.dlc = dlc;
      o.proxy_key = proxy_key.first;
      o.proxy_iv = proxy_key.second;
      rp->relay.reset(new v1_21::Relay(core, std::move(o), &relay_host));
      if (rp->relay->start_error()) return crash(rp->relay->start_error()->line());
    } else {
      std::string folder = plat::join_path(save_root, g.name);
      say(std::string(g.name) + " relay on udp " + ip + ":" + std::to_string(rp->port) + ", saves in " + folder);
      o.log_path = plat::join_path(log_dir, "v100.jsonl");
      o.save_path = plat::join_path(folder, "player.json");
      rp->relay.reset(new v1_00::Relay(core, std::move(o), &relay_host));
      if (rp->relay->start_error()) {
        say(std::string("game version ") + g.name + " not started: " + rp->relay->start_error()->line());
        continue;
      }
    }
    relays.push_back(std::move(rp));
  }

  std::string lan;
  {
    std::optional<std::string> advertised;
    if (!setting(core.config().advertised_ip(), &advertised, &code)) return code;
    lan = advertised ? *advertised : lan_ip();
  }
  {
    AdminOptions ao;
    for (const GameVersion& g : kVersions) {
      admin::RelayView view;
      view.version = g.name;
      view.port = static_cast<int>(port + g.port_offset);
      for (const auto& rp : relays) {
        if (rp->name == g.name) view.relay = rp->relay.get();
      }
      ao.relays.push_back(view);
    }
    ao.facts.version = kVersion;
    ao.facts.address = server_address(lan, https_bound);
    ao.facts.lan_ip = lan_ip();
    ao.facts.http_port = http_bound;
    ao.facts.https_port = https_bound;
    ao.facts.discovery_port = discovery_port();
    ao.facts.relay_ip = ip;
    ao.facts.server_id = py::hex(settings.server_id);
    ao.facts.started_wall = started_wall;
    ao.facts.started_mono = started_mono;
#if defined(TCR_TEST_HOOKS)
    ao.test_mode = true;
#endif
    ao.hooks = hooks;
    start_admin_page(core, std::move(ao));
  }
  say("\nServer address: " + server_address(lan, https_bound) +
      "\nReady. Start The Crew on your PS4 or PC; the game finds the server on the local network by itself.\n"
      "Stop the server with Ctrl+C.\n");

  int thread_number = https_bound ? 3 : 2;
  for (size_t k = 0; k < relays.size(); ++k) {
    size_t i = (k + 1) % relays.size();
    RelayPort& rp = *relays[i];
    if (i != 0) ++thread_number;
    std::string bind_ip, line;
    bool bound = plat::bind_address(rp.ip, &bind_ip, &line);
    if (bound && !port_ok(rp.port)) {
      bound = false;
      line = kOverflow;
    }
    plat::Error err;
    if (bound && !rp.loop.open(bind_ip, rp.port, false, &err)) {
      bound = false;
      line = std::string(plat::python_error_name(err)) + ": " + plat::python_error_text(err);
    }
    if (!bound) {
      if (i == 0) return crash(line);
      say_error("Exception in thread Thread-" + std::to_string(thread_number) + " (serve_forever):");
      crash(line);
      continue;
    }
    v1_21::Relay* relay = rp.relay.get();
    int relay_port = rp.port;
    rp.loop.start(
        65535,
        [relay](const std::string& data, const plat::Addr& from) {
          std::vector<server::Datagram> out;
          Bytes bytes(data.begin(), data.end());
          v1_21::Addr addr{from.ip, from.port};
          v1_21::DatagramResult r = relay->handle_datagram(bytes, addr);
          if (r.raised) {
            relay->handler_error(bytes, addr, "Traceback (most recent call last):\n" + r.raised->line() + "\n");
          } else {
            for (const v1_21::Packet& p : r.packets) {
              out.push_back(server::Datagram{std::string(p.data.begin(), p.data.end()), plat::Addr{p.dst.ip, p.dst.port}});
            }
          }
          return out;
        },
        [relay](const plat::Addr& to, const std::string& error) {
          relay->send_failed(v1_21::Addr{to.ip, to.port}, error);
        },
        [relay, hooks, relay_port](size_t packets) {
          {
            server::CoreLock lock;
            relay->datagram_done();
          }
          if (hooks) hooks->datagram_done(relay_port, packets);
        });
  }
  if (hooks) hooks->start();
  g_serving.store(true);
  starting.reset();
  for (;;) plat::sleep_seconds(3600);
}

}
