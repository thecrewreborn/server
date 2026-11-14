#pragma once

#include <map>
#include <memory>
#include <string>

#include "core/core.h"
#include "core/files.h"
#include "core/wire.h"

namespace tcr {
namespace debug {

struct State {
  std::unique_ptr<wire::PendingRegistry> registry;
  std::map<int64_t, std::shared_ptr<wire::Partial>> partials;
};

bool call(State& state, const std::string& name, const std::string& args_json, std::string* result);

bool run_v1_21(Core& core, Files& files, const std::string& args_json, std::string* result);

}
}
