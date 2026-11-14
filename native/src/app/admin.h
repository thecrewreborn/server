#pragma once

#include <string>
#include <vector>

#include "core/admin.h"

namespace tcr {
class Core;
namespace app {

class TestHooks;

struct AdminOptions {
  std::vector<tcr::admin::RelayView> relays;
  tcr::admin::Facts facts;
  bool test_mode = false;
  TestHooks* hooks = nullptr;
};

void start_admin_page(Core& core, AdminOptions options);

void request_stop();
void request_restart();

}
}
