#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "platform/platform.h"
#include "server/http_proto.h"

namespace tcr::tls {
class ServerContext;
}

namespace tcr::server {

struct Limits {
  size_t max_body = 300000;
  size_t drain_limit = 2000000;
  int max_conn_per_ip = 16;
  int max_conn = 512;
  double first_line_timeout = 15;
  double body_timeout = 30;
  double timeout = 120;
  double drain_timeout = 5;
};

struct Request {
  std::string method;
  std::string path;
  std::string version;
  http::Headers headers;
  std::string auth;
  std::string raw;
  std::string host;
  std::string client_ip;
  int client_port = 0;
  bool tls = false;
};

struct Response {
  int status = 200;
  std::string payload;
  bool binary = false;
  std::string log_reply;
  bool raised = false;
  std::string exception;
  std::string content_type;
  std::vector<std::pair<std::string, std::string>> headers;
};

struct Hooks {
  std::function<Response(const Request&)> route;
  std::function<void(const std::string& client_ip, const std::string& text, bool verified)> say;
  std::function<bool(const std::string& auth, std::string* raised)> live_ticket;
};

class HttpServer {
 public:
  explicit HttpServer(Hooks hooks, Limits limits = Limits());
  ~HttpServer();
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  bool start(const std::string& ip, int port, tls::ServerContext* tls, const std::string& log_path, plat::Error* err);
  int port() const { return port_; }
  int open_connections();
  void stop();

  struct Shared;

 private:
  std::shared_ptr<Shared> shared_;
  int port_ = 0;
};

}
