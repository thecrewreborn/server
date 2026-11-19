#include "app/system.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <map>

#include "core/config.h"
#include "core/core.h"
#include "core/ubi.h"
#include "core/util.h"
#include "core/version.h"
#include "mbedtls/md.h"
#include "platform/platform.h"
#include "server/capture.h"
#include "server/core_lock.h"
#include "server/tls.h"

namespace tcr::app {

std::string console_text(const std::string& text) {
  bool plain = true;
  for (unsigned char c : text) {
    if (c == 0xED) {
      plain = false;
      break;
    }
  }
  if (plain) return text;
  std::string out;
  for (uint32_t cp : py::code_points(text)) {
    if (cp >= 0xD800 && cp <= 0xDFFF) {
      out += '?';
    } else {
      py::append_utf8(out, cp);
    }
  }
  return out;
}

void say(const std::string& text) { plat::print_line(console_text(text)); }
void say_error(const std::string& text) { plat::print_error_line(console_text(text)); }

std::string home_folder() {
  std::string home = plat::env_var("TCREBORN_HOME");
  return home.empty() ? plat::dir_name(plat::executable_path()) : home;
}

std::string config_file(const std::string& home) {
  if (std::getenv("TCREBORN_CONFIG")) return plat::env_var("TCREBORN_CONFIG");
  return plat::join_path(home, "config.ini");
}

std::string first_config_file(const std::string& home) {
  std::string path = plat::env_var("TCREBORN_CONFIG");
  return path.empty() ? plat::join_path(home, "config.ini") : path;
}

double SystemEnv::time() { return fixed_clock_ ? clock_ : plat::wall_time(); }

double SystemEnv::monotonic() { return fixed_clock_ ? clock_ : plat::monotonic(); }

void SystemEnv::urandom(uint8_t* out, size_t n) {
  if (deterministic_) {
    ++counter_;
    Random(counter_).randbytes(out, n);
    return;
  }
  if (!plat::random_bytes(out, n)) {
    say_error("the system's random source failed");
    std::_Exit(1);
  }
}

Random SystemEnv::unseeded_random() {
  if (deterministic_) return Random(0);
  return random_from_urandom(*this);
}

LocalTime SystemEnv::now_local() {
  if (fixed_clock_) return local_time(clock_);
  plat::LocalTime t = plat::local_now();
  LocalTime out;
  out.year = t.year;
  out.month = t.month;
  out.day = t.day;
  out.hour = t.hour;
  out.minute = t.minute;
  out.second = t.second;
  out.microsecond = t.microsecond;
  return out;
}

bool SystemEnv::now_utc(int64_t* sec, int* us) {
  if (fixed_clock_) return split_timestamp(clock_, sec, us);
  int64_t ns = plat::wall_time_ns();
  *sec = ns / 1000000000;
  *us = static_cast<int>(ns % 1000000000 / 1000);
  if (*us < 0) {
    *us += 1000000;
    *sec -= 1;
  }
  return true;
}

LocalTime SystemEnv::local_time(double t) {
  int64_t sec = 0;
  int us = 0;
  if (!split_timestamp(t, &sec, &us)) return LocalTime();
  std::time_t tt = static_cast<std::time_t>(sec);
  std::tm tm{};
#if defined(_WIN32)
  if (localtime_s(&tm, &tt) != 0) return utc_time(t);
#else
  if (!localtime_r(&tt, &tm)) return utc_time(t);
#endif
  LocalTime out;
  out.year = tm.tm_year + 1900;
  out.month = tm.tm_mon + 1;
  out.day = tm.tm_mday;
  out.hour = tm.tm_hour;
  out.minute = tm.tm_min;
  out.second = tm.tm_sec;
  out.microsecond = us;
  return out;
}

std::optional<std::string> SystemEnv::config_stamp() {
  FileStat st;
  OsError err;
  if (!files_.stat(config_path_, &st, &err)) return std::nullopt;
  return std::to_string(st.mtime_ns) + " " + std::to_string(st.size) + " " + std::to_string(st.ino);
}

std::optional<std::string> SystemEnv::config_read() {
  std::string data;
  OsError err;
  if (!files_.read(config_path_, &data, &err)) return std::nullopt;
  return data;
}

void SystemEnv::hash_unlocked(void (*fn)(void*), void* arg) { server::run_hash_unlocked(fn, arg); }

bool SystemRoute::local_ip_for(const std::string& peer, std::string* ip, std::string* error) {
  plat::Error err;
  if (plat::route_ip(peer, 9, ip, &err)) return true;
  *error = plat::python_error_text(err);
  return false;
}

std::string lan_ip() {
  std::string ip;
  plat::Error err;
  if (!plat::route_ip("10.255.255.255", 1, &ip, &err)) return "127.0.0.1";
  return ip;
}

bool SystemRelayHost::append_log(const std::string& path, const std::string& line, std::string* why) {
  return capture::append_log(path, line, why);
}

void SystemRelayHost::log_failed(const std::string& path, const std::string& why) { capture::log_failed(path, why); }

bool SystemRelayHost::local_ip_for(const std::string& peer, std::string* ip, Raised* raised) {
  plat::Error err;
  if (plat::route_ip(peer, 9, ip, &err)) return true;
  *raised = Raised{plat::python_error_name(err), plat::python_error_text(err)};
  return false;
}

const char kServerName[] = "The Crew Reborn server";

namespace {

constexpr char kReportUrl[] = "https://crewreborn-stats.knutwurst.workers.dev/ingest";
constexpr double kDay = 86400.0;
constexpr double kMinGap = 3600.0;
constexpr double kFirst = 60.0;
constexpr double kSample = 30.0;
constexpr double kPersist = 600.0;
constexpr double kTimeout = 15.0;

struct PlayerSeen {
  int64_t missions = 0, activities = 0;
  std::string crew;
};

struct Last {
  double at = 0.0;
  bool ok = false;
  int status = 0;
  std::string error, payload;
};

struct Report {
  Core* core = nullptr;
  ReportSetup setup;
  std::string url;
  double first = kFirst, tick = 15.0;
  std::string path;
  std::string sha256;
  bool on = false;
  std::string id;
  Last last;
  double next = 0.0;
  std::map<std::string, PlayerSeen> players;
  int64_t peak = 0;
  std::map<std::string, int64_t> carry;
  double sampled = 0.0, persisted = 0.0;
};
Report* g_report = nullptr;

std::string file_sha256(const std::string& path) {
  std::string data;
  plat::Error err;
  if (!plat::read_file(path, &data, &err)) return "";
  unsigned char out[32];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const unsigned char*>(data.data()),
             data.size(), out);
  return py::hex(out, sizeof(out));
}

std::string text_sha256(const std::string& text) {
  unsigned char out[32];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const unsigned char*>(text.data()),
             text.size(), out);
  return py::hex(out, sizeof(out));
}

json::Value str_value(const std::string& s) { return json::Value::string(s); }
json::Value int_value(int64_t n) { return json::Value::integer(n); }

template <class T>
T setting_or(const config::Result<T>& r, T fallback) {
  return r.ok() ? *r.value : fallback;
}

json::Value build_report(Report& r, const std::string& kind, std::map<std::string, int64_t>* used) {
  Core& core = *r.core;
  const config::Config& c = core.config();
  json::Value out = json::Value::object();
  out.set("v", int_value(1));
  out.set("id", str_value(r.id));
  out.set("kind", str_value(kind));
  out.set("name", str_value(kServerName));
  out.set("version", str_value(kVersion));
  out.set("page_title", str_value(r.setup.page_title));
  out.set("login_title", str_value(r.setup.login_title));
  json::Value build = json::Value::object();
  build.set("record", str_value(kBuild));
  build.set("signature", str_value(kBuildSignature));
  build.set("id", str_value(text_sha256(kBuild).substr(0, 12)));
  build.set("sha256", str_value(r.sha256));
  out.set("build", build);
  plat::SystemInfo info = plat::system_info();
  json::Value system = json::Value::object();
  system.set("family", str_value(info.family));
  system.set("version", str_value(info.version));
  system.set("arch", str_value(info.arch));
  system.set("android_api", info.android_api ? int_value(info.android_api) : json::Value::null());
  out.set("system", system);
  out.set("uptime", int_value(static_cast<int64_t>(plat::monotonic() - r.setup.started_mono)));

  json::Value settings = json::Value::object();
  settings.set("dlc", json::Value::boolean(setting_or(c.dlc(), std::string("none")) == "all"));
  settings.set("chat", json::Value::boolean(setting_or(c.chat(), false)));
  settings.set("multiplayer", str_value(setting_or(c.multiplayer(), std::string("?"))));
  settings.set("link_home", json::Value::boolean(setting_or(c.link_home(), true)));
  settings.set("club_rewards", json::Value::boolean(setting_or(c.club_rewards(), true)));
  settings.set("abilities", str_value(setting_or(c.abilities(), std::string("?"))));
  settings.set("crew_credits_refill", json::Value::boolean(setting_or(c.crew_credits_refill(), true)));
  settings.set("bots", json::Value::boolean(setting_or(c.bots(), false)));
  settings.set("afk_kick", json::Value::boolean(setting_or(c.afk_kick(), false)));
  std::optional<std::string> advertised = setting_or(c.advertised_ip(), std::optional<std::string>());
  settings.set("address", str_value(!advertised ? "unset" : ubi::trusted(*advertised) ? "private" : "public"));
  settings.set("https", json::Value::boolean(r.setup.https_port != 0));
  settings.set("default_ports", json::Value::boolean((r.setup.http_port == 80 || r.setup.http_port == 8080) &&
                                                     (r.setup.https_port == 443 || r.setup.https_port == 8443) &&
                                                     r.setup.relay_port == 4000));
  std::optional<bool> ui = setting_or(c.ui_enabled(), std::optional<bool>());
  settings.set("admin_page", json::Value::boolean(!ui || *ui));
  settings.set("admin_password", json::Value::boolean(!setting_or(c.ui_password(), std::string()).empty()));
  settings.set("save_path", json::Value::boolean(setting_or(c.save_path_configured(), false)));
  out.set("settings", settings);

  json::Value counts = json::Value::object();
  *used = core.tally().counts();
  for (const auto& kv : *used) counts.set(kv.first, int_value(kv.second));
  std::map<std::string, int64_t> distinct = core.tally().distinct();
  for (const auto& kv : r.carry) distinct[kv.first] += kv.second;
  for (const auto& kv : distinct) counts.set("players." + kv.first, int_value(kv.second));
  counts.set("peak_players", int_value(r.peak));
  json::Value names = admin::names(core);
  if (names.is_array()) counts.set("names", int_value(static_cast<int64_t>(names.size())));
  out.set("counts", counts);
  return out;
}

void sample(Report& r) {
  json::Value list = admin::players(*r.core, r.setup.relays);
  if (!list.is_array()) return;
  std::map<std::string, PlayerSeen> now;
  for (const json::Value& e : list.items()) {
    const json::Value* version = e.get("version");
    const json::Value* name = e.get("name");
    if (!version || !name || !version->is_string() || !name->is_string()) continue;
    PlayerSeen seen;
    if (const json::Value* m = e.get("missions_done")) seen.missions = m->as_i64().value_or(0);
    if (const json::Value* a = e.get("activities_done")) seen.activities = a->as_i64().value_or(0);
    if (const json::Value* crew = e.get("crew")) seen.crew = crew->is_string() ? crew->as_string() : "";
    std::string key = version->as_string() + "\n" + name->as_string();
    auto before = r.players.find(key);
    if (before != r.players.end()) {
      if (seen.missions > before->second.missions) {
        r.core->tally().add("missions_done", seen.missions - before->second.missions);
      }
      if (seen.activities > before->second.activities) {
        r.core->tally().add("activities_done", seen.activities - before->second.activities);
      }
      if (!seen.crew.empty() && seen.crew != before->second.crew) r.core->tally().add("crew_joins");
    }
    now[key] = seen;
  }
  r.players = std::move(now);
  r.peak = std::max(r.peak, static_cast<int64_t>(r.players.size()));
}

void persist(Report& r, bool running) {
  json::Value doc = json::Value::object();
  doc.set("id", str_value(r.id));
  doc.set("running", json::Value::boolean(running));
  json::Value last = json::Value::object();
  last.set("at", json::Value::number(r.last.at));
  last.set("ok", json::Value::boolean(r.last.ok));
  last.set("status", int_value(r.last.status));
  last.set("error", str_value(r.last.error));
  last.set("payload", str_value(r.last.payload));
  doc.set("last", last);
  json::Value pending = json::Value::object();
  json::Value counts = json::Value::object();
  for (const auto& kv : r.core->tally().counts()) counts.set(kv.first, int_value(kv.second));
  pending.set("counts", counts);
  json::Value distinct = json::Value::object();
  std::map<std::string, int64_t> d = r.core->tally().distinct();
  for (const auto& kv : r.carry) d[kv.first] += kv.second;
  for (const auto& kv : d) distinct.set(kv.first, int_value(kv.second));
  pending.set("distinct", distinct);
  pending.set("peak", int_value(r.peak));
  doc.set("pending", pending);
  std::string text = json::dumps(doc, 1);
  plat::Error err;
  plat::write_atomic(r.path, text.data(), text.size(), true, &err);
  r.persisted = plat::monotonic();
}

bool load(Report& r) {
  std::string data;
  plat::Error err;
  if (!plat::read_file(r.path, &data, &err)) return false;
  std::optional<json::Value> doc = json::loads_utf8(data, json::kHttpMaxDepth);
  if (!doc || !doc->is_object()) return false;
  const json::Value* id = doc->get("id");
  if (!id || !id->is_string() || id->as_string().size() != 32) return false;
  r.id = id->as_string();
  if (const json::Value* last = doc->get("last"); last && last->is_object()) {
    auto num = [&](const char* k) { const json::Value* v = last->get(k); return v && v->is_number() ? v : nullptr; };
    if (const json::Value* v = num("at")) r.last.at = v->is_float() ? v->as_float() : double(v->as_i64().value_or(0));
    if (const json::Value* v = num("status")) r.last.status = static_cast<int>(v->as_i64().value_or(0));
    if (const json::Value* v = last->get("ok"); v && v->is_bool()) r.last.ok = v->as_bool();
    if (const json::Value* v = last->get("error"); v && v->is_string()) r.last.error = v->as_string();
    if (const json::Value* v = last->get("payload"); v && v->is_string()) r.last.payload = v->as_string();
  }
  if (const json::Value* running = doc->get("running"); running && running->is_bool() && running->as_bool()) {
    r.core->tally().add("unclean_stops");
  }
  if (const json::Value* pending = doc->get("pending"); pending && pending->is_object()) {
    if (const json::Value* counts = pending->get("counts"); counts && counts->is_object()) {
      for (const json::Member& m : counts->members()) {
        std::optional<int64_t> n = m.value.as_i64();
        if (n && *n > 0 && *n < 1000000000) r.core->tally().add(m.key, *n);
      }
    }
    if (const json::Value* distinct = pending->get("distinct"); distinct && distinct->is_object()) {
      for (const json::Member& m : distinct->members()) {
        std::optional<int64_t> n = m.value.as_i64();
        if (n && *n > 0 && *n < 1000000000) r.carry[m.key] += *n;
      }
    }
    if (const json::Value* peak = pending->get("peak")) r.peak = std::max<int64_t>(0, peak->as_i64().value_or(0));
  }
  return true;
}

bool new_id(std::string* id) {
  uint8_t raw[16];
  if (!plat::random_bytes(raw, sizeof(raw))) return false;
  *id = py::hex(raw, sizeof(raw));
  return true;
}

void plan_first(Report& r) {
  double now = plat::monotonic();
  double next = now + r.first;
  if (r.last.ok && r.last.at > 0) {
    double since = plat::wall_time() - r.last.at;
    if (since >= 0 && since < kMinGap) next = std::max(next, now + (kMinGap - since));
  }
  r.next = next;
}

void report_loop(Report& r) {
  bool started = true;
  std::string kind = "start";
  for (;;) {
    std::string payload;
    std::map<std::string, int64_t> used;
    {
      server::CoreLock lock;
      bool want = setting_or(r.core->config().report_enabled(), false);
      if (!want) {
        if (r.on || (started && plat::path_exists(r.path))) {
          plat::Error err;
          plat::remove_file(r.path, &err);
          if (r.on) say("Status report: off; " + r.path + " deleted");
        }
        r.on = false;
        r.id.clear();
        r.last = Last();
        r.next = 0;
        r.carry.clear();
        r.peak = 0;
        r.players.clear();
        const std::map<std::string, int64_t> all = r.core->tally().counts();
        r.core->tally().settle(all);
      } else if (!r.on) {
        bool known = load(r);
        if (!known && !new_id(&r.id)) {
          say("Status report: no random id (the system's random source failed); not sent");
        } else {
          r.on = true;
          say("Status report: on (shortly after the start, then once a day; the admin page shows what is sent)");
          plan_first(r);
          kind = "start";
          persist(r, true);
        }
      }
      started = false;
      const double now = plat::monotonic();
      if (r.on && now - r.sampled >= kSample) {
        sample(r);
        r.sampled = now;
      }
      if (r.on && r.next > 0 && now >= r.next) {
        payload = json::dumps_compact(build_report(r, kind, &used));
      } else if (r.on && now - r.persisted >= kPersist) {
        persist(r, true);
      }
    }
    if (!payload.empty()) {
      tls::PostResult result;
      bool sent = tls::post(r.url, payload, std::string("TheCrewReborn-Server/") + kVersion, kTimeout, &result);
      bool ok = sent && result.status >= 200 && result.status < 300;
      server::CoreLock lock;
      if (!r.on) continue;
      r.last.at = plat::wall_time();
      r.last.ok = ok;
      r.last.status = result.status;
      r.last.error = ok ? "" : (sent ? "HTTP " + std::to_string(result.status) : result.error);
      r.last.payload = payload;
      if (ok) {
        r.core->tally().settle(used);
        r.carry.clear();
        r.peak = static_cast<int64_t>(r.players.size());
        say("Status report sent");
      } else {
        say("Status report not sent (" + r.last.error + "); next try in a day");
      }
      r.next = plat::monotonic() + kDay;
      kind = "daily";
      persist(r, true);
    }
    plat::sleep_seconds(r.tick);
  }
}

}

void start_status_report(Core& core, ReportSetup setup) {
  Report* r = new Report;
  r->core = &core;
  r->setup = std::move(setup);
  r->path = plat::join_path(core.home(), "report.json");
  r->url = kReportUrl;
  r->sha256 = file_sha256(plat::executable_path());
  g_report = r;
  if (r->url.empty()) return;
  plat::start_thread([r]() { report_loop(*r); });
}

json::Value status_report_view(Core& core) {
  json::Value out = json::Value::object();
  Report* r = g_report;
  out.set("endpoint", str_value(r ? r->url : std::string(kReportUrl)));
  out.set("enabled", json::Value::boolean(setting_or(core.config().report_enabled(), false)));
  out.set("chosen", json::Value::boolean(setting_or(core.config().report_chosen(), true)));
  if (!r) return out;
  out.set("id", r->on ? str_value(r->id) : json::Value::null());
  if (r->on && r->last.at > 0) {
    json::Value last = json::Value::object();
    last.set("at", json::Value::number(r->last.at));
    last.set("ok", json::Value::boolean(r->last.ok));
    last.set("status", int_value(r->last.status));
    last.set("error", str_value(r->last.error));
    last.set("payload", str_value(r->last.payload));
    out.set("last", last);
  } else {
    out.set("last", json::Value::null());
  }
  out.set("next_at", r->on && r->next > 0 ? json::Value::number(plat::wall_time() + (r->next - plat::monotonic()))
                                          : json::Value::null());
  std::map<std::string, int64_t> used;
  Report preview = *r;
  if (!preview.on) preview.id = "(made when it is switched on)";
  out.set("preview", str_value(json::dumps(build_report(preview, "daily", &used), 2)));
  return out;
}

void status_report_stopping() {
  Report* r = g_report;
  if (!r) return;
  auto mark = [r]() {
    if (r->on) persist(*r, false);
  };
  if (server::holds_core_lock()) {
    mark();
  } else {
    server::CoreLock lock;
    mark();
  }
}

}
