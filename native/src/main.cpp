#include <cstdlib>
#include <string>
#include <vector>

#include "app/server.h"
#include "app/system.h"
#include "platform/platform.h"

int main(int argc, char** argv) {
  if (!tcr::plat::init()) {
    tcr::app::say_error("cannot start the network");
    return 1;
  }
  std::vector<std::string> args(argv + 1, argv + argc);
  if (!args.empty() && args[0] == "accounts") {
    return tcr::app::run_accounts(std::vector<std::string>(args.begin() + 1, args.end()));
  }
  if (!args.empty() && args[0] == "console-password") {
    return tcr::app::run_console_password(std::vector<std::string>(args.begin() + 1, args.end()));
  }
  std::_Exit(tcr::app::run_server(args));
}
