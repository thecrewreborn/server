#pragma once

#include <cstddef>
#include <string>

#include "core/discovery.h"
#include "server/http_proto.h"

namespace tcr {
class Core;
namespace app {

class SystemEnv;

#if defined(TCR_TEST_HOOKS)

int discovery_port();
void start_pause();

class TestHooks {
 public:
  static TestHooks* from_environment(Core& core, SystemEnv& env);

  void http_client(const http::Headers& headers, std::string* ip, int* port, std::string* host);
  void datagram_done(int port, size_t packets);
  void start();

 private:
  TestHooks(Core& core, SystemEnv& env) : core_(core), env_(env) {}
  void run();
  std::string command(const std::string& line);

  Core& core_;
  SystemEnv& env_;
};

#else

inline int discovery_port() { return discovery::DISCOVERY_PORT; }
inline void start_pause() {}

class TestHooks {
 public:
  static TestHooks* from_environment(Core&, SystemEnv&) { return nullptr; }
  void http_client(const http::Headers&, std::string*, int*, std::string*) {}
  void datagram_done(int, size_t) {}
  void start() {}
};

#endif

}
}
