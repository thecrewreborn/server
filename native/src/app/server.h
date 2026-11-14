#pragma once

#include <string>
#include <vector>

namespace tcr::app {

int run_server(const std::vector<std::string>& args);
int run_accounts(const std::vector<std::string>& args);
int run_console_password(const std::vector<std::string>& args);

}
