#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/core.h"
#include "core/discovery.h"

namespace tcr {
namespace debug {

class TableRoute final : public discovery::Route {
 public:
  bool local_ip_for(const std::string& peer, std::string* ip, std::string* error) override {
    auto it = table.find(peer);
    if (it == table.end()) {
      *error = "[Errno 51] Network is unreachable";
      return false;
    }
    *ip = it->second;
    return true;
  }
  std::map<std::string, std::string> table;
};

struct PeersState {
  TableRoute route;
  std::unique_ptr<discovery::Discovery> discovery;
};

bool peers_call(Core& core, PeersState& state, const std::string& name, const std::string& args_json,
                std::string* result);

}
}
