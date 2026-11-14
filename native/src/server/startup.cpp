#include "server/startup.h"

namespace tcr::server {

int fallback_port(int port) {
  if (port == 80) return 8080;
  if (port == 443) return 8443;
  return 0;
}

bool bind_with_fallback(const std::string& name, int port, const std::function<bool(int, plat::Error*)>& serve,
                        int* used, std::string* exit_message, std::string* crash_line) {
  plat::Error err;
  if (serve(port, &err)) {
    *used = port;
    return true;
  }
  int alt = fallback_port(port);
  if (!alt) {
    *exit_message = name + ": cannot open tcp port " + std::to_string(port) + ": " + err.text;
    return false;
  }
  std::string why = plat::is_addr_in_use(err) ? "is in use by another program" : "needs more rights";
  std::string hint;
#if defined(__linux__)
  if (port < 1024) {
    hint = " (Linux: ports below 1024 need root, or once: sudo setcap cap_net_bind_service=+ep " +
           plat::executable_path() + ")";
  }
#endif
  plat::print_line(name + ": tcp port " + std::to_string(port) + " " + why + hint + "; using " + std::to_string(alt) +
                   ". The game learns the port from discovery; a server.txt / reborn.ini with a fixed address needs :" +
                   std::to_string(alt) + ".");
  plat::Error alt_err;
  if (!serve(alt, &alt_err)) {
    if (crash_line) {
      exit_message->clear();
      *crash_line = std::string(plat::python_error_name(alt_err)) + ": " + plat::python_error_text(alt_err);
    } else {
      *exit_message = name + ": cannot open tcp port " + std::to_string(alt) + ": " + alt_err.text;
    }
    return false;
  }
  *used = alt;
  return true;
}

int udp_in_use(const std::vector<int>& ports) {
  for (int port : ports) {
    if (!plat::udp_port_free(port)) return port;
  }
  return 0;
}

}
