#pragma once

#include <functional>
#include <string>
#include <vector>

#include "platform/platform.h"

namespace tcr::server {

int fallback_port(int port);

bool bind_with_fallback(const std::string& name, int port, const std::function<bool(int, plat::Error*)>& serve,
                        int* used, std::string* exit_message, std::string* crash_line = nullptr);

int udp_in_use(const std::vector<int>& ports);

}
