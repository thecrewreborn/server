#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/logtext.h"
#include "platform/platform.h"
#include "server/capture.h"
#include "server/core_lock.h"
#include "server/http_proto.h"
#include "server/http_server.h"
#include "server/startup.h"
#include "server/tls.h"
#include "server/udp_loop.h"

using namespace tcr;

namespace {

server::Response echo(const server::Request& req) {
  server::Response r;
  const std::string& p = req.path;
  if (p.compare(0, 8, "/status/") == 0 && p.size() >= 11) {
    r.status = std::atoi(p.substr(8, 3).c_str());
    r.payload = "{\"status\": true}";
    return r;
  }
  if (p.compare(0, 4, "/bin") == 0) {
    r.binary = true;
    r.payload = std::string("\x89PNG\r\n\x1a\n", 8) + req.raw;
    return r;
  }
  if (p.compare(0, 6, "/raise") == 0) {
    r.raised = true;
    r.exception = "TypeError: unhashable type: 'list'";
    return r;
  }
  if (p.compare(0, 7, "/ticket") == 0) {
    r.payload = "{\"ticket\": \"abc\", \"token\": \"def\", \"n\": 1}";
    r.log_reply = "{\"ticket\": \"***\", \"token\": \"***\", \"n\": 1}";
    return r;
  }
  r.payload = "{\"method\": " + tcr::json_quote_latin1(req.method) + ", \"path\": " +
              tcr::json_quote_latin1(req.path) + ", \"host\": " + tcr::json_quote_latin1(req.host) +
              ", \"client\": " + tcr::json_quote_latin1(req.client_ip) + ", \"auth\": " +
              tcr::json_quote_latin1(req.auth) + ", \"raw\": " + tcr::json_quote_latin1(req.raw) + "}";
  return r;
}

}

int main(int argc, char** argv) {
  if (!plat::init()) return 1;
  int http_port = -1, https_port = -1;
  std::string cert, key, cert_ip, log;
  std::vector<int> udp_ports;
  size_t udp_max = 65535;
  server::Limits limits;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--http") {
      http_port = std::atoi(next().c_str());
    } else if (a == "--https") {
      https_port = std::atoi(next().c_str());
    } else if (a == "--cert") {
      cert = next();
    } else if (a == "--key") {
      key = next();
    } else if (a == "--cert-ip") {
      cert_ip = next();
    } else if (a == "--log") {
      log = next();
    } else if (a == "--udp") {
      udp_ports.push_back(std::atoi(next().c_str()));
    } else if (a == "--udp-max") {
      udp_max = static_cast<size_t>(std::atoll(next().c_str()));
    } else if (a == "--max-conn") {
      limits.max_conn = std::atoi(next().c_str());
    } else if (a == "--max-conn-per-ip") {
      limits.max_conn_per_ip = std::atoi(next().c_str());
    } else if (a == "--first-line") {
      limits.first_line_timeout = std::atof(next().c_str());
    } else if (a == "--body") {
      limits.body_timeout = std::atof(next().c_str());
    } else if (a == "--timeout") {
      limits.timeout = std::atof(next().c_str());
    } else if (a == "--drain") {
      limits.drain_timeout = std::atof(next().c_str());
    } else if (a == "--max-body") {
      limits.max_body = static_cast<size_t>(std::atoll(next().c_str()));
    } else if (a == "--drain-limit") {
      limits.drain_limit = static_cast<size_t>(std::atoll(next().c_str()));
    } else if (a == "--rotate-bytes") {
      capture::set_rotate_bytes(std::atoll(next().c_str()));
    } else if (a == "--bind-test") {
      std::string name = next();
      int port = std::atoi(next().c_str());
      int code = std::atoi(next().c_str());
      int calls = 0, used = 0;
      std::string message;
      bool ok = server::bind_with_fallback(
          name, port,
          [&](int, plat::Error* e) {
            if (calls++ == 0) {
              *e = plat::system_error(code);
              return false;
            }
            return true;
          },
          &used, &message);
      plat::print_line(ok ? "USED " + std::to_string(used) : "EXIT " + message);
      return 0;
    } else if (a == "--udp-selftest") {
      server::UdpLoop idle;
      plat::Error e;
      bool unopened_ok = !idle.start(512, nullptr, nullptr) &&
                         !idle.send(server::Datagram{"x", plat::Addr{"127.0.0.1", 9}}, &e);
      idle.serve(512, nullptr, nullptr);
      server::UdpLoop loop;
      if (!loop.open("127.0.0.1", 0, false, &e)) return 1;
      int port = loop.port();
      loop.start(512, [](const std::string& data, const plat::Addr& from) {
        return std::vector<server::Datagram>{server::Datagram{"echo:" + data, from}};
      }, nullptr);
      plat::Socket client = plat::udp_open("127.0.0.1", 0, false, false, &e);
      {
        server::CoreLock lock;
        plat::udp_sendto(client, "a", 1, plat::Addr{"127.0.0.1", port}, &e);
        plat::sleep_seconds(0.3);
        loop.close();
      }
      bool echoed = plat::wait_readable(client, 2.0, &e) > 0;
      server::UdpLoop self;
      if (!self.open("127.0.0.1", 0, false, &e)) return 1;
      int self_port = self.port();
      self.start(512, [&self](const std::string&, const plat::Addr&) {
        self.close();
        return std::vector<server::Datagram>();
      }, nullptr);
      plat::udp_sendto(client, "b", 1, plat::Addr{"127.0.0.1", self_port}, &e);
      plat::sleep_seconds(0.3);
      plat::close_socket(client);
      plat::print_line(std::string("SELFTEST ") + (unopened_ok && echoed ? "ok" : "failed"));
      return 0;
    } else if (a == "--append") {
      std::string path = next();
      std::string line = next() + "\n";
      std::string why;
      if (!capture::append_log(path, line, &why)) {
        plat::print_line("FAILED " + why);
        return 1;
      }
      return 0;
    } else {
      plat::print_error_line("unknown argument " + a);
      return 2;
    }
  }

  server::Hooks hooks;
  hooks.route = echo;
  hooks.say = [](const std::string& ip, const std::string& text, bool verified) {
    plat::print_line(std::string("SAY ") + (verified ? "1 " : "0 ") + ip + " " + text);
  };
  hooks.live_ticket = [](const std::string& auth, std::string* raised) {
    if (auth.find("raise-ticket") != std::string::npos) {
      *raised = "RecursionError: maximum recursion depth exceeded while decoding a JSON array from a unicode string";
      return false;
    }
    return auth.find("live-ticket") != std::string::npos;
  };

  plat::Error err;
  std::unique_ptr<server::HttpServer> http, https;
  std::unique_ptr<tls::ServerContext> context;
  std::string ready = "READY";
  if (http_port >= 0) {
    http.reset(new server::HttpServer(hooks, limits));
    if (!http->start("0.0.0.0", http_port, nullptr, log, &err)) {
      plat::print_line("FAILED http: " + plat::python_error_text(err));
      return 1;
    }
    ready += " http=" + std::to_string(http->port());
  }
  if (https_port >= 0) {
    bool wrote = false;
    std::string why;
    if (!tls::ensure(cert, key, cert_ip, &wrote, &why)) {
      plat::print_line("FAILED ensure: " + why);
      return 1;
    }
    if (wrote) plat::print_line("created the HTTPS certificate " + cert);
    context.reset(new tls::ServerContext);
    if (!context->load(cert, key, &why)) {
      plat::print_line("FAILED load: " + why);
      return 1;
    }
    https.reset(new server::HttpServer(hooks, limits));
    if (!https->start("0.0.0.0", https_port, context.get(), log, &err)) {
      plat::print_line("FAILED https: " + plat::python_error_text(err));
      return 1;
    }
    ready += " https=" + std::to_string(https->port());
  }
  std::vector<std::unique_ptr<server::UdpLoop>> loops;
  std::string udp_list;
  for (int port : udp_ports) {
    std::unique_ptr<server::UdpLoop> loop(new server::UdpLoop);
    if (!loop->open("0.0.0.0", port, false, &err)) {
      plat::print_line("FAILED udp: " + plat::python_error_text(err));
      return 1;
    }
    loop->start(
        udp_max,
        [](const std::string& data, const plat::Addr& from) {
          std::vector<server::Datagram> out;
          if (data.compare(0, 3, "to:") == 0) {
            std::string rest = data.substr(3);
            size_t colon = rest.find(':');
            plat::Addr to;
            to.ip = rest.substr(0, colon);
            to.port = std::atoi(rest.substr(colon + 1).c_str());
            out.push_back(server::Datagram{"forward", to});
          }
          out.push_back(server::Datagram{"echo:" + std::to_string(data.size()) + ":" + data, from});
          return out;
        },
        [](const plat::Addr& to, const std::string& error) {
          plat::print_line("SENDFAIL " + to.ip + ":" + std::to_string(to.port) + " " + error);
        });
    udp_list += (udp_list.empty() ? "" : ",") + std::to_string(loop->port());
    loops.push_back(std::move(loop));
  }
  if (!udp_list.empty()) ready += " udp=" + udp_list;
  plat::print_line(ready);
  for (;;) plat::sleep_seconds(3600);
}
