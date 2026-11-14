#include "app/test_hooks.h"

#include "core/v1_21/factions.h"

#include <cstdio>
#include <cstdlib>

#include "app/system.h"
#include "core/core.h"
#include "core/discovery.h"
#include "core/json.h"
#include "core/ubi.h"
#include "core/util.h"
#include "platform/platform.h"
#include "server/core_lock.h"

namespace tcr::app {

int discovery_port() {
  std::string port = plat::env_var("TCREBORN_TEST_DISCOVERY_PORT");
  return port.empty() ? discovery::DISCOVERY_PORT : std::atoi(port.c_str());
}

void start_pause() {
  std::string pause = plat::env_var("TCREBORN_TEST_PAUSE");
  if (!pause.empty()) plat::sleep_seconds(std::atof(pause.c_str()));
}

TestHooks* TestHooks::from_environment(Core& core, SystemEnv& env) {
  if (plat::env_var("TCREBORN_TEST_HOOKS") != "1") return nullptr;
  say("WARNING: test hooks on (TCREBORN_TEST_HOOKS): commands on stdin and a client address per request");
  return new TestHooks(core, env);
}

void TestHooks::http_client(const http::Headers& headers, std::string* ip, int* port, std::string* host) {
  if (std::optional<std::string> client = headers.get("X-Tcr-Test-Client")) {
    size_t colon = client->rfind(':');
    if (colon != std::string::npos) {
      *ip = client->substr(0, colon);
      *port = std::atoi(client->c_str() + colon + 1);
    }
  }
  if (std::optional<std::string> h = headers.get("X-Tcr-Test-Host")) *host = *h;
}

void TestHooks::datagram_done(int port, size_t packets) {
  plat::print_line("TEST datagram " + std::to_string(port) + " " + std::to_string(packets));
}

void TestHooks::start() {
  plat::start_thread([this]() { run(); });
}

void TestHooks::run() {
  char buf[4096];
  while (std::fgets(buf, sizeof(buf), stdin)) {
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    std::string reply;
    {
      server::CoreLock lock;
      reply = command(line);
    }
    plat::print_line("TEST " + reply);
  }
}

std::string TestHooks::command(const std::string& line) {
  size_t space = line.find(' ');
  std::string name = line.substr(0, space);
  std::string rest = space == std::string::npos ? "" : line.substr(space + 1);
  auto word = [&rest]() {
    size_t s = rest.find(' ');
    std::string w = rest.substr(0, s);
    rest = s == std::string::npos ? "" : rest.substr(s + 1);
    return w;
  };
  if (name == "hooks") {
    double epoch = std::atof(word().c_str());
    uint64_t counter = std::strtoull(word().c_str(), nullptr, 10);
    env_.set_clock(epoch);
    env_.set_deterministic(counter);
    core_.set_module_random(Random(0));
    return "true";
  }
  if (name == "defaults") {
    Overrides& o = core_.overrides();
    o.multiplayer = std::string("off");
    o.link_home = true;
    o.reward_tables = false;
    o.car_catalog = false;
    return "true";
  }
  if (name == "clock") {
    env_.set_clock(std::atof(word().c_str()));
    return "true";
  }
  if (name == "open_link") {
    double minutes = std::atof(word().c_str());
    Raised raised;
    std::optional<bool> opened = core_.ubi().accounts().open_link(rest, minutes, &raised);
    if (!opened) return json::dumps(json::Value::string(raised.line()));
    return *opened ? "true" : "false";
  }
  if (name == "ticket_owner") {
    bool refresh = word() == "1";
    Raised raised;
    std::optional<std::pair<std::string, std::string>> who = core_.ubi().ticket_owner(rest, refresh, &raised);
    if (!raised.type.empty()) return json::dumps(json::Value::string(raised.line()));
    if (!who) return "null";
    json::Value v = json::Value::array();
    v.push(json::Value::string(who->first));
    v.push(json::Value::string(who->second));
    return json::dumps(v);
  }
  if (name == "factions_path") return json::dumps(json::Value::string(v1_21::factions::path(core_)));
  return json::dumps(json::Value::string("unknown command " + name));
}

}
