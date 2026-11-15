#pragma once

#include <cstddef>
#include <string>

#include "core/discovery.h"
#include "server/http_proto.h"

namespace tcr {
class Core;
namespace app {

class SystemEnv;

inline int discovery_port() { return discovery::DISCOVERY_PORT; }
inline void start_pause() {}

class TestHooks {
 public:
  static TestHooks* from_environment(Core&, SystemEnv&) { return nullptr; }
  void http_client(const http::Headers&, std::string*, int*, std::string*) {}
  void datagram_done(int, size_t) {}
  void start() {}
};

}
}
