#include "tcr.h"

#if defined(_WIN32)
#define _CRT_RAND_S
#include <stdlib.h>
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/random.h>
#endif
#endif

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/core.h"
#include "core/env.h"
#include "core/random.h"
#include "core/ubi.h"
#include "core/util.h"
#include "core/v1_00/handlers.h"
#include "core/v1_00/relay.h"
#include "probe.h"
#include "core/v1_21/relay.h"
#include "core/version.h"
#include "debug_calls.h"
#include "debug_peers.h"
#include "platform/files.h"

struct tcr_out {
  struct Item {
    std::vector<uint8_t> data;
    int kind = TCR_OUT_BYTES;
    std::string ip;
    int port = 0;
  };
  std::vector<Item> items;

  void clear() { items.clear(); }
  void add(const uint8_t* data, size_t n, int kind, const char* ip = "", int port = 0) {
    Item item;
    if (n) item.data.assign(data, data + n);
    item.kind = kind;
    item.ip = ip ? ip : "";
    item.port = port;
    items.push_back(std::move(item));
  }
  void add_text(const std::string& text, int kind) {
    add(reinterpret_cast<const uint8_t*>(text.data()), text.size(), kind);
  }
};

namespace {

void system_random(uint8_t* out, size_t n) {
#if defined(_WIN32)
  while (n > 0) {
    unsigned int value = 0;
    if (rand_s(&value) != 0) std::abort();
    size_t take = n < sizeof(value) ? n : sizeof(value);
    std::memcpy(out, &value, take);
    out += take;
    n -= take;
  }
#else
  while (n > 0) {
    size_t take = n < 256 ? n : 256;
    if (getentropy(out, take) != 0) std::abort();
    out += take;
    n -= take;
  }
#endif
}

class CapiEnv final : public tcr::Env {
 public:
  double time() override {
    if (fixed_clock_) return clock_;
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
  }
  double monotonic() override {
    if (fixed_clock_) return clock_;
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  void urandom(uint8_t* out, size_t n) override {
    if (deterministic_) {
      ++counter_;
      tcr::Random(counter_).randbytes(out, n);
      return;
    }
    system_random(out, n);
  }
  tcr::Random unseeded_random() override {
    if (deterministic_) return tcr::Random(0);
    return tcr::random_from_urandom(*this);
  }
  tcr::LocalTime local_time(double t) override {
    int64_t sec = 0;
    int us = 0;
    if (!tcr::split_timestamp(t, &sec, &us)) return tcr::LocalTime();
    std::time_t tt = static_cast<std::time_t>(sec);
    std::tm tm{};
#if defined(_WIN32)
    if (localtime_s(&tm, &tt) != 0) return tcr::utc_time(t);
#else
    if (!localtime_r(&tt, &tm)) return tcr::utc_time(t);
#endif
    tcr::LocalTime out;
    out.year = tm.tm_year + 1900;
    out.month = tm.tm_mon + 1;
    out.day = tm.tm_mday;
    out.hour = tm.tm_hour;
    out.minute = tm.tm_min;
    out.second = tm.tm_sec;
    out.microsecond = us;
    return out;
  }
  void print_line(const std::string& line) override { lines_.push_back(line); }
  std::vector<std::string> take_lines() {
    std::vector<std::string> out;
    out.swap(lines_);
    return out;
  }

  bool provides_config() const override { return true; }
  std::optional<std::string> config_stamp() override {
    if (!config_) return std::nullopt;
    return std::to_string(config_version_);
  }
  std::optional<std::string> config_read() override { return config_; }
  void set_config(const char* text) {
    if (text && *text) {
      config_ = std::string(text);
    } else {
      config_.reset();
    }
    ++config_version_;
  }

  void hash_unlocked(void (*fn)(void*), void* arg) override {
    if (fail_hashes_) return;
    bool held = lock_ && owner_ == std::this_thread::get_id();
    if (held) {
      owner_ = std::thread::id();
      lock_->unlock();
    }
    {
      std::unique_lock<std::mutex> slots(slots_lock_);
      slots_free_.wait(slots, [this] { return hashing_ < hash_threads(); });
      ++hashing_;
    }
    fn(arg);
    {
      std::lock_guard<std::mutex> slots(slots_lock_);
      --hashing_;
    }
    slots_free_.notify_one();
    if (held) {
      lock_->lock();
      owner_ = std::this_thread::get_id();
    }
  }
  static unsigned hash_threads() {
    unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 2 : (n < 4 ? n : 4);
  }
  void bind_lock(std::mutex* lock) { lock_ = lock; }
  void held_by_this_thread(bool held) { owner_ = held ? std::this_thread::get_id() : std::thread::id(); }

  void set_clock(double now) {
    fixed_clock_ = true;
    clock_ = now;
  }
  void set_deterministic() {
    deterministic_ = true;
    counter_ = 0;
  }
  void fail_hashes() { fail_hashes_ = true; }

 private:
  bool fixed_clock_ = false;
  bool fail_hashes_ = false;
  double clock_ = 0.0;
  bool deterministic_ = false;
  uint64_t counter_ = 0;
  std::vector<std::string> lines_;
  std::optional<std::string> config_;
  uint64_t config_version_ = 0;
  std::mutex* lock_ = nullptr;
  std::thread::id owner_;
  std::mutex slots_lock_;
  std::condition_variable slots_free_;
  unsigned hashing_ = 0;
};

bool to_latin1(const char* s, std::string* out) {
  out->clear();
  if (!s) return true;
  for (uint32_t cp : tcr::py::code_points(s)) {
    if (cp > 0xFF) return false;
    *out += static_cast<char>(cp);
  }
  return true;
}

std::string text_or_empty(const char* s) { return s ? std::string(s) : std::string(); }

}

class CapiLogHost final : public tcr::v1_21::RelayHost {
 public:
  explicit CapiLogHost(CapiEnv& env) : env_(env) {}
  bool append_log(const std::string& path, const std::string& line, std::string* why) override {
    const long kRotate = 256L * 1024 * 1024;
    std::FILE* f = std::fopen(path.c_str(), "ab");
    if (!f) return fail(path, why);
    std::fseek(f, 0, SEEK_END);
    bool full = std::ftell(f) >= kRotate;
    if (!full) std::fwrite(line.data(), 1, line.size(), f);
    std::fclose(f);
    if (full) {
      std::rename(path.c_str(), (path + ".1").c_str());
      f = std::fopen(path.c_str(), "ab");
      if (!f) return fail(path, why);
      std::fwrite(line.data(), 1, line.size(), f);
      std::fclose(f);
    }
    return true;
  }
  bool local_ip_for(const std::string& peer, std::string* ip, tcr::Raised* raised) override {
    if (!fail_routes_) return RelayHost::local_ip_for(peer, ip, raised);
    *raised = tcr::Raised{"OSError", "[Errno 51] Network is unreachable"};
    return false;
  }
  void fail_routes() { fail_routes_ = true; }
  void log_failed(const std::string& path, const std::string& why) override {
    double now = env_.monotonic();
    if (now - warned_ < 60) return;
    warned_ = now;
    env_.print_line(tcr::stamped(env_, "cannot write " + path + ": " + why +
                                           " (the server goes on without this log; this message repeats at most "
                                           "once a minute)"));
  }

 private:
  bool fail_routes_ = false;
  static bool fail(const std::string& path, std::string* why) {
    int code = errno;
    *why = "[Errno " + std::to_string(code) + "] " + std::strerror(code) + ": " + tcr::py::repr(path);
    return false;
  }
  CapiEnv& env_;
  double warned_ = -1e9;
};

struct tcr_core {
  CapiEnv env;
  tcr::platform::SystemFiles files;
  tcr::Core core;
  CapiLogHost log_host{env};
  std::mutex lock;
  std::map<std::thread::id, tcr_out> outs;
  tcr::debug::PeersState peers_debug;

  tcr_core(const char* home, const char* config) : env(), core(env, text_or_empty(home), text_or_empty(config)) {
    core.set_files(&files);
    env.set_config(config);
    env.bind_lock(&lock);
  }
  tcr_out& out() {
    tcr_out& o = outs[std::this_thread::get_id()];
    o.clear();
    return o;
  }
};

namespace {

class Held {
 public:
  explicit Held(tcr_core* core) : core_(core) {
    core_->lock.lock();
    core_->env.held_by_this_thread(true);
  }
  ~Held() {
    core_->env.held_by_this_thread(false);
    core_->lock.unlock();
  }
  Held(const Held&) = delete;
  Held& operator=(const Held&) = delete;

 private:
  tcr_core* core_;
};

}

struct tcr_relay {
  tcr_core* core = nullptr;
  std::string version;
  std::unique_ptr<tcr::v1_21::Relay> relay;
  std::map<std::thread::id, tcr_out> outs;
  tcr_out& out() {
    tcr_out& o = outs[std::this_thread::get_id()];
    o.clear();
    return o;
  }
};

struct tcr_debug_random {
  tcr::Random r;
};

struct tcr_debug {
  tcr::debug::State state;
  tcr_out out;
};

extern "C" {

const char* tcr_status_text(int status) {
  switch (status) {
    case TCR_OK:
      return "ok";
    case TCR_E_NOT_IMPLEMENTED:
      return "not implemented in the native core yet";
    case TCR_E_ARGUMENT:
      return "bad argument";
    case TCR_E_FAILED:
      return "failed";
    default:
      return status >= 100 && status <= 599 ? "HTTP status" : "unknown status";
  }
}

const char* tcr_version(void) { return tcr::kVersion; }

size_t tcr_out_count(const tcr_out* out) { return out ? out->items.size() : 0; }

const uint8_t* tcr_out_data(const tcr_out* out, size_t i, size_t* n) {
  if (!out || i >= out->items.size()) {
    if (n) *n = 0;
    return nullptr;
  }
  const auto& item = out->items[i];
  if (n) *n = item.data.size();
  static const uint8_t kEmpty = 0;
  return item.data.empty() ? &kEmpty : item.data.data();
}

int tcr_out_kind(const tcr_out* out, size_t i) {
  if (!out || i >= out->items.size()) return -1;
  return out->items[i].kind;
}

const char* tcr_out_ip(const tcr_out* out, size_t i) {
  if (!out || i >= out->items.size()) return "";
  return out->items[i].ip.c_str();
}

int tcr_out_port(const tcr_out* out, size_t i) {
  if (!out || i >= out->items.size()) return 0;
  return out->items[i].port;
}

tcr_core* tcr_core_new(const char* home, const char* config_ini_text) {
  if (!home) return nullptr;
  return new tcr_core(home, config_ini_text);
}

void tcr_core_free(tcr_core* core) { delete core; }

void tcr_test_hooks(tcr_core* core, double epoch) {
  if (!core) return;
  Held held(core);
  core->env.set_clock(epoch);
  core->env.set_deterministic();
  core->core.set_module_random(tcr::Random(0));
}

void tcr_test_suite_defaults(tcr_core* core) {
  if (!core) return;
  Held held(core);
  tcr::Overrides& o = core->core.overrides();
  o.multiplayer = std::string("off");
  o.link_home = true;
  o.reward_tables = false;
  o.car_catalog = false;
}

void tcr_set_clock(tcr_core* core, double now) {
  if (!core) return;
  Held held(core);
  core->env.set_clock(now);
}

void tcr_set_config(tcr_core* core, const char* config_ini_text) {
  if (!core) return;
  Held held(core);
  core->env.set_config(config_ini_text);
}

int tcr_http(tcr_core* core, const char* method, const char* path, const char* body_json, const char* host,
             const char* client_ip, int client_port, const char* auth, const uint8_t* raw, size_t raw_n,
             tcr_out** reply) {
  if (!core || !method || !path || !client_ip || !reply || (raw_n && !raw)) return TCR_E_ARGUMENT;
  Held held(core);
  tcr_out& out = core->out();
  *reply = &out;
  std::string m, p, a;
  if (!to_latin1(method, &m) || !to_latin1(path, &p) || !to_latin1(auth, &a)) return TCR_E_ARGUMENT;
  tcr::json::Value body = tcr::json::Value::object();
  if (body_json && *body_json) {
    std::optional<tcr::json::Value> parsed = tcr::json::loads(body_json);
    if (!parsed) return TCR_E_ARGUMENT;
    if (parsed->is_object()) body = std::move(*parsed);
  }
  std::string raw_bytes(reinterpret_cast<const char*>(raw), raw_n);
  tcr::ubi::Reply r = core->core.ubi().route(m, p, body, host ? host : "", client_ip, client_port, a, raw_bytes);
  if (r.raised) {
    out.add_text(r.exception.line(), TCR_OUT_TEXT);
    return TCR_E_FAILED;
  }
  if (r.binary) {
    out.add(reinterpret_cast<const uint8_t*>(r.bytes.data()), r.bytes.size(), TCR_OUT_BYTES);
  } else {
    out.add_text(tcr::json::dumps(r.json), TCR_OUT_JSON);
  }
  return r.status;
}

int tcr_accounts_open_link(tcr_core* core, const char* name, double minutes) {
  if (!core || !name) return TCR_E_ARGUMENT;
  Held held(core);
  tcr::Raised raised;
  std::optional<bool> opened = core->core.ubi().accounts().open_link(name, minutes, &raised);
  if (!opened) return TCR_E_FAILED;
  return *opened ? 1 : 0;
}

static int ticket_owner(tcr_core* core, const char* ticket, bool refresh, tcr_out** owner) {
  if (!core || !ticket || !owner) return TCR_E_ARGUMENT;
  Held held(core);
  tcr_out& out = core->out();
  *owner = &out;
  tcr::Raised raised;
  std::optional<std::pair<std::string, std::string>> who = core->core.ubi().ticket_owner(ticket, refresh, &raised);
  if (!raised.type.empty()) {
    out.add_text(raised.line(), TCR_OUT_TEXT);
    return TCR_E_FAILED;
  }
  if (!who) return 0;
  out.add_text(who->first, TCR_OUT_TEXT);
  out.add_text(who->second, TCR_OUT_TEXT);
  return 1;
}

int tcr_ticket_owner(tcr_core* core, const char* ticket, tcr_out** owner) {
  return ticket_owner(core, ticket, false, owner);
}

int tcr_ticket_owner_refresh(tcr_core* core, const char* ticket, tcr_out** owner) {
  return ticket_owner(core, ticket, true, owner);
}

int tcr_active(tcr_core* core, const char* name) {
  if (!core || !name) return TCR_E_ARGUMENT;
  Held held(core);
  core->core.ubi().active(name);
  return TCR_OK;
}

int tcr_console(tcr_core* core, tcr_out** lines) {
  if (!core || !lines) return TCR_E_ARGUMENT;
  Held held(core);
  tcr_out& out = core->out();
  *lines = &out;
  for (const std::string& line : core->env.take_lines()) out.add_text(line, TCR_OUT_TEXT);
  return TCR_OK;
}

int tcr_probe(tcr_core* core, const char* version, tcr_out** json) {
  if (!core || !version || !json) return TCR_E_ARGUMENT;
  if (std::strcmp(version, "01.21") != 0 && std::strcmp(version, "01.00") != 0) return TCR_E_ARGUMENT;
  Held held(core);
  tcr_out& out = core->out();
  *json = &out;
  out.add_text(std::strcmp(version, "01.21") == 0 ? tcr::v1_21::probe_json(core->core)
                                                  : tcr::v1_00::probe_json(core->core),
               TCR_OUT_JSON);
  return TCR_OK;
}

tcr_relay* tcr_relay_new(tcr_core* core, const char* version, const char* server_ip, int port,
                         const char* save_path) {
  if (!core || !version || !server_ip || !save_path) return nullptr;
  if (std::strcmp(version, "01.21") != 0 && std::strcmp(version, "01.00") != 0) return nullptr;
  if (port < 0 || port > 65535) return nullptr;
  Held held(core);
  std::unique_ptr<tcr_relay> relay(new tcr_relay());
  relay->core = core;
  relay->version = version;
  tcr::v1_21::RelayOptions o;
  o.ip = server_ip;
  o.port = port;
  o.save_path = save_path;
  if (relay->version == "01.21") {
    relay->relay.reset(new tcr::v1_21::Relay(core->core, std::move(o)));
  } else {
    relay->relay.reset(new tcr::v1_00::Relay(core->core, std::move(o)));
  }
  if (relay->relay->start_error()) return nullptr;
  return relay.release();
}

tcr_relay* tcr_relay_new_with(tcr_core* core, const char* options_json, const char** error) {
  static thread_local std::string last_error;
  if (error) *error = nullptr;
  if (!core || !options_json) return nullptr;
  std::optional<tcr::json::Value> o = tcr::json::loads(options_json);
  if (!o || !o->is_object()) return nullptr;
  Held held(core);
  auto text = [&](const char* key, const std::string& fallback) {
    const tcr::json::Value* v = o->get(key);
    return v && v->is_string() ? v->as_string() : fallback;
  };
  std::unique_ptr<tcr_relay> relay(new tcr_relay());
  relay->core = core;
  relay->version = text("version", "01.21");
  if (relay->version != "01.21" && relay->version != "01.00") return nullptr;
  if (relay->version == "01.00" && o->get("dlc")) {
    last_error = "TypeError: tcreborn.games.v1_21.relay.Server.__init__() got multiple values for keyword argument "
                 "'dlc'";
    if (error) *error = last_error.c_str();
    return nullptr;
  }
  tcr::v1_21::RelayOptions opt;
  opt.ip = text("ip", "");
  const tcr::json::Value* port = o->get("port");
  std::optional<int64_t> p = port ? port->as_i64() : std::nullopt;
  if (!p || *p < 0 || *p > 65535) return nullptr;
  opt.port = static_cast<int>(*p);
  opt.save_path = text("save_path", "");
  opt.log_path = text("log_path", "");
  opt.abilities = text("abilities", opt.abilities);
  opt.unlocks = text("unlocks", opt.unlocks);
  opt.dlc = text("dlc", opt.dlc);
  opt.proxy_key = tcr::py::fromhex(text("proxy_key", "")).value_or(tcr::Bytes());
  opt.proxy_iv = tcr::py::fromhex(text("proxy_iv", "")).value_or(tcr::Bytes());
  if (relay->version == "01.21") {
    relay->relay.reset(new tcr::v1_21::Relay(core->core, std::move(opt), &core->log_host));
  } else {
    relay->relay.reset(new tcr::v1_00::Relay(core->core, std::move(opt), &core->log_host));
  }
  if (relay->relay->start_error()) {
    last_error = relay->relay->start_error()->line();
    if (error) *error = last_error.c_str();
    return nullptr;
  }
  return relay.release();
}

void tcr_relay_free(tcr_relay* relay) { delete relay; }

int tcr_relay_datagram(tcr_relay* relay, const uint8_t* data, size_t n, const char* ip, int port, tcr_out** out) {
  if (!relay || (n && !data) || !ip || !out) return TCR_E_ARGUMENT;
  Held held(relay->core);
  tcr_out& o = relay->out();
  *out = &o;
  if (!relay->relay) return TCR_E_NOT_IMPLEMENTED;
  tcr::v1_21::DatagramResult r =
      relay->relay->handle_datagram(tcr::Bytes(data, data + n), tcr::v1_21::Addr{ip, port});
  if (r.raised) {
    o.add_text(r.raised->line(), TCR_OUT_TEXT);
    return TCR_E_FAILED;
  }
  for (const tcr::v1_21::Packet& p : r.packets) {
    o.add(p.data.data(), p.data.size(), TCR_OUT_BYTES, p.dst.ip.c_str(), p.dst.port);
  }
  return TCR_OK;
}

int tcr_relay_session_key(tcr_relay* relay, const char* ip, int port, tcr_out** key) {
  if (!relay || !ip || !key) return TCR_E_ARGUMENT;
  Held held(relay->core);
  tcr_out& o = relay->out();
  *key = &o;
  if (!relay->relay) return TCR_E_NOT_IMPLEMENTED;
  const tcr::v1_21::Session* s = relay->relay->session(tcr::v1_21::Addr{ip, port});
  if (!s) return 0;
  o.add(s->key.data(), s->key.size(), TCR_OUT_BYTES);
  o.add(s->iv.data(), s->iv.size(), TCR_OUT_BYTES);
  return s->connected ? 2 : 1;
}

tcr_debug_random* tcr_debug_random_new(const uint8_t* seed_le, size_t n) {
  if (n && !seed_le) return nullptr;
  tcr_debug_random* r = new tcr_debug_random();
  r->r.seed_le_bytes(seed_le, n);
  return r;
}

void tcr_debug_random_free(tcr_debug_random* r) { delete r; }

int tcr_debug_random_getrandbits(tcr_debug_random* r, uint32_t k, uint8_t* out, size_t out_n) {
  size_t need = (static_cast<size_t>(k) + 7) / 8;
  if (!r || (need && !out) || out_n < need) return TCR_E_ARGUMENT;
  r->r.getrandbits_le(k, out);
  return TCR_OK;
}

int tcr_debug_random_randbytes(tcr_debug_random* r, size_t n, uint8_t* out) {
  if (!r || (n && !out)) return TCR_E_ARGUMENT;
  r->r.randbytes(out, n);
  return TCR_OK;
}

double tcr_debug_random_random(tcr_debug_random* r) { return r ? r->r.random() : -1.0; }

double tcr_debug_random_uniform(tcr_debug_random* r, double a, double b) { return r ? r->r.uniform(a, b) : -1.0; }

int tcr_debug_random_randbelow(tcr_debug_random* r, uint64_t n, uint64_t* result) {
  if (!r || n == 0 || !result) return TCR_E_ARGUMENT;
  *result = r->r.randbelow(n);
  return TCR_OK;
}

int tcr_debug_random_randrange(tcr_debug_random* r, int64_t start, int64_t stop, int64_t step, int64_t* result) {
  if (!r || !result) return TCR_E_ARGUMENT;
  std::optional<int64_t> v = r->r.randrange(start, stop, step);
  if (!v) return TCR_E_ARGUMENT;
  *result = *v;
  return TCR_OK;
}

int tcr_debug_random_randint(tcr_debug_random* r, int64_t a, int64_t b, int64_t* result) {
  if (!r || !result) return TCR_E_ARGUMENT;
  std::optional<int64_t> v = r->r.randint(a, b);
  if (!v) return TCR_E_ARGUMENT;
  *result = *v;
  return TCR_OK;
}

int tcr_debug_random_choices(tcr_debug_random* r, const double* weights, size_t n, size_t* result) {
  if (!r || (n && !weights) || !result) return TCR_E_ARGUMENT;
  std::optional<size_t> v = r->r.choices_index(weights, n);
  if (!v) return TCR_E_ARGUMENT;
  *result = *v;
  return TCR_OK;
}

int tcr_debug_core_urandom(tcr_core* core, size_t n, uint8_t* out) {
  if (!core || (n && !out)) return TCR_E_ARGUMENT;
  core->core.env().urandom(out, n);
  return TCR_OK;
}

double tcr_debug_core_time(tcr_core* core) { return core ? core->core.env().time() : -1.0; }

double tcr_debug_core_monotonic(tcr_core* core) { return core ? core->core.env().monotonic() : -1.0; }

double tcr_debug_core_module_random(tcr_core* core) { return core ? core->core.module_random().random() : -1.0; }

double tcr_debug_core_unseeded_random(tcr_core* core) {
  if (!core) return -1.0;
  tcr::Random r = core->core.env().unseeded_random();
  return r.random();
}

tcr_debug* tcr_debug_new(void) { return new tcr_debug(); }

void tcr_debug_free(tcr_debug* d) { delete d; }

int tcr_debug_v1_21(tcr_core* core, const char* args_json, tcr_out** result) {
  if (!core || !args_json || !result) return TCR_E_ARGUMENT;
  Held held(core);
  tcr_out& out = core->out();
  *result = &out;
  std::string text;
  std::optional<tcr::json::Value> args = tcr::json::loads(args_json);
  const tcr::json::Value* fn = args && args->is_object() ? args->get("fn") : nullptr;
  if (fn && fn->is_string() && fn->as_string() == "env.fail_hashes") {
    core->env.fail_hashes();
    out.add_text("{}", TCR_OUT_JSON);
    return TCR_OK;
  }
  if (fn && fn->is_string() && fn->as_string() == "env.fail_routes") {
    core->log_host.fail_routes();
    out.add_text("{}", TCR_OUT_JSON);
    return TCR_OK;
  }
  bool ok = tcr::debug::run_v1_21(core->core, core->files, args_json, &text);
  out.add_text(text, ok ? TCR_OUT_JSON : TCR_OUT_TEXT);
  return ok ? TCR_OK : TCR_E_ARGUMENT;
}

int tcr_debug_call(tcr_debug* d, const char* name, const char* args_json, tcr_out** result) {
  if (!d || !name || !args_json || !result) return TCR_E_ARGUMENT;
  d->out.clear();
  *result = &d->out;
  std::string text;
  bool ok = tcr::debug::call(d->state, name, args_json, &text);
  d->out.add_text(text, ok ? TCR_OUT_JSON : TCR_OUT_TEXT);
  return ok ? TCR_OK : TCR_E_ARGUMENT;
}

int tcr_debug_peers(tcr_core* core, const char* name, const char* args_json, tcr_out** result) {
  if (!core || !name || !args_json || !result) return TCR_E_ARGUMENT;
  Held held(core);
  tcr_out& out = core->out();
  *result = &out;
  std::string text;
  bool ok = tcr::debug::peers_call(core->core, core->peers_debug, name, args_json, &text);
  out.add_text(text, ok ? TCR_OUT_JSON : TCR_OUT_TEXT);
  return ok ? TCR_OK : TCR_E_ARGUMENT;
}

}
