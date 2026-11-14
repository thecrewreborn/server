#include "server/http_server.h"

#include <cstring>
#include <utility>
#include <vector>

#include "core/version.h"
#include "server/capture.h"
#include "server/core_lock.h"
#include "server/stream.h"
#include "server/tls.h"

namespace tcr::server {

namespace {

constexpr size_t kMaxLine = 65536;
constexpr size_t kMaxHeaders = 100;
constexpr size_t kReadChunk = 8192;
constexpr int kBacklog = 5;

plat::Mutex g_log_lock;

std::string lower_ascii(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

bool ends_with(std::string_view s, std::string_view tail) {
  return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

enum class Flow { kOk, kTimeout, kFail };

}

struct HttpServer::Shared {
  Hooks hooks;
  Limits limits;
  tls::ServerContext* tls = nullptr;
  std::string log_path;
  plat::Socket listener = plat::kNoSocket;
  std::atomic<bool> running{false};
  std::atomic<bool> accept_done{true};
  plat::Mutex conn_lock;
  int open = 0;
  std::map<std::string, int> per_ip;

  void release(const std::string& ip) {
    plat::Lock lock(conn_lock);
    --open;
    auto it = per_ip.find(ip);
    if (it != per_ip.end() && --it->second <= 0) per_ip.erase(it);
  }
};

namespace {

class Reader {
 public:
  Reader(Stream& s, double deadline, double* sock_timeout) : s_(s), deadline_(deadline), sock_timeout_(sock_timeout) {}

  void wait_idle(double idle) {
    deadline_ = -1;
    idle_ = idle;
  }
  void arm(double seconds) {
    deadline_ = plat::monotonic() + seconds;
    idle_ = -1;
  }
  void clear_deadline() { deadline_ = -1; }
  void set_first_line(double first_line) { first_line_ = first_line; }

  Flow readline(size_t limit, std::string* line) {
    line->clear();
    for (;;) {
      size_t avail = buf_.size() - pos_;
      size_t nl = buf_.find('\n', pos_);
      if (nl != std::string::npos && nl - pos_ + 1 <= limit) {
        take(nl - pos_ + 1, line);
        return Flow::kOk;
      }
      if (avail >= limit) {
        take(limit, line);
        return Flow::kOk;
      }
      size_t got = 0;
      Flow f = fill(&got);
      if (f != Flow::kOk) return f;
      if (got == 0) {
        take(buf_.size() - pos_, line);
        return Flow::kOk;
      }
    }
  }

  Flow read(size_t n, std::string* out) {
    out->clear();
    while (buf_.size() - pos_ < n) {
      size_t got = 0;
      Flow f = fill(&got);
      if (f != Flow::kOk) return f;
      if (got == 0) break;
    }
    take(std::min(n, buf_.size() - pos_), out);
    return Flow::kOk;
  }

  const IoStatus& failure() const { return fail_; }

 private:
  void take(size_t n, std::string* out) {
    out->assign(buf_, pos_, n);
    pos_ += n;
    if (pos_ == buf_.size()) {
      buf_.clear();
      pos_ = 0;
    }
  }

  Flow fill(size_t* got) {
    uint8_t chunk[kReadChunk];
    IoStatus st;
    if (idle_ >= 0) {
      *sock_timeout_ = idle_;
      st = s_.read(chunk, sizeof(chunk), idle_, got);
      if (st.ok()) {
        deadline_ = plat::monotonic() + first_line_;
        idle_ = -1;
      }
    } else if (deadline_ >= 0) {
      double left = deadline_ - plat::monotonic();
      if (left <= 0) return Flow::kTimeout;
      *sock_timeout_ = left;
      st = s_.read(chunk, sizeof(chunk), left, got);
    } else {
      st = s_.read(chunk, sizeof(chunk), *sock_timeout_, got);
    }
    if (st.kind == IoStatus::kTimeout) return Flow::kTimeout;
    if (!st.ok()) {
      fail_ = st;
      return Flow::kFail;
    }
    if (pos_ > 0) {
      buf_.erase(0, pos_);
      pos_ = 0;
    }
    buf_.append(reinterpret_cast<const char*>(chunk), *got);
    return Flow::kOk;
  }

  Stream& s_;
  std::string buf_;
  size_t pos_ = 0;
  double deadline_;
  double idle_ = -1;
  double first_line_ = 15;
  double* sock_timeout_;
  IoStatus fail_;
};

class Handler {
 public:
  Handler(HttpServer::Shared& srv, Stream& s, const plat::Addr& peer, bool tls)
      : srv_(srv),
        lim_(srv.limits),
        s_(s),
        peer_(peer),
        tls_(tls),
        sock_timeout_(srv.limits.timeout),
        reader_(s, plat::monotonic() + srv.limits.first_line_timeout, &sock_timeout_) {
    reader_.set_first_line(lim_.first_line_timeout);
  }

  Flow handle() {
    close_connection_ = true;
    Flow f = handle_one_request();
    while (f != Flow::kFail && !close_connection_) f = handle_one_request();
    return f;
  }

  const IoStatus& failure() const { return fail_.kind != IoStatus::kOk ? fail_ : reader_.failure(); }

 private:

  Flow handle_one_request() {
    if (requests_) reader_.wait_idle(lim_.timeout);
    ++requests_;
    Flow f = one_request();
    if (f == Flow::kTimeout) {
      close_connection_ = true;
      return Flow::kOk;
    }
    return f;
  }

  Flow one_request() {
    std::string line;
    Flow f = reader_.readline(kMaxLine + 1, &line);
    if (f != Flow::kOk) return f;
    if (line.size() > kMaxLine) {
      requestline_.clear();
      request_version_.clear();
      command_.clear();
      return send_error(414);
    }
    if (line.empty()) {
      close_connection_ = true;
      return Flow::kOk;
    }
    bool ok = false;
    f = parse_request(line, &ok);
    reader_.arm(lim_.body_timeout);
    if (f != Flow::kOk || !ok) return f;
    if (command_ != "GET" && command_ != "POST" && command_ != "PUT" && command_ != "DELETE") {
      return send_error(501, "Unsupported method (" + tcr::repr_latin1(command_) + ")");
    }
    return serve();
  }

  Flow parse_request(const std::string& raw_line, bool* ok) {
    *ok = false;
    command_.clear();
    request_version_ = "HTTP/0.9";
    close_connection_ = true;
    std::string_view rl(raw_line);
    while (!rl.empty() && (rl.back() == '\r' || rl.back() == '\n')) rl.remove_suffix(1);
    requestline_ = std::string(rl);
    std::vector<std::string> words = tcr::split_ws_latin1(requestline_);
    if (words.empty()) return Flow::kOk;
    if (words.size() >= 3) {
      const std::string& version = words.back();
      http::Version v = http::parse_version(version);
      if (!v.valid) return send_error(400, "Bad request version (" + tcr::repr_latin1(version) + ")");
      if (v.major > 1 || (v.major == 1 && v.minor >= 1)) close_connection_ = false;
      if (v.major >= 2) return send_error(505, "Invalid HTTP version (" + v.base + ")");
      request_version_ = version;
    }
    if (words.size() < 2 || words.size() > 3) {
      return send_error(400, "Bad request syntax (" + tcr::repr_latin1(requestline_) + ")");
    }
    bool http09 = false;
    if (words.size() == 2) {
      close_connection_ = true;
      if (words[0] != "GET") return send_error(400, "Bad HTTP/0.9 request type (" + tcr::repr_latin1(words[0]) + ")");
      http09 = true;
    }
    command_ = words[0];
    path_ = words[1];
    if (path_.compare(0, 2, "//") == 0) {
      size_t keep = path_.find_first_not_of('/');
      path_ = "/" + (keep == std::string::npos ? std::string() : path_.substr(keep));
    }
    if (http09) {
      headers_ = http::Headers();
      *ok = true;
      return Flow::kOk;
    }
    std::string block;
    size_t count = 0;
    for (;;) {
      std::string hl;
      Flow f = reader_.readline(kMaxLine + 1, &hl);
      if (f != Flow::kOk) return f;
      if (hl.size() > kMaxLine) {
        return send_error(431, "Line too long", "got more than 65536 bytes when reading header line");
      }
      block += hl;
      if (++count > kMaxHeaders) return send_error(431, "Too many headers", "got more than 100 headers");
      if (hl == "\r\n" || hl == "\n" || hl.empty()) break;
    }
    headers_ = http::Headers::parse(block);
    std::string conntype = lower_ascii(headers_.get("Connection").value_or(""));
    if (conntype == "close") {
      close_connection_ = true;
    } else if (conntype == "keep-alive") {
      close_connection_ = false;
    }
    std::string expect = lower_ascii(headers_.get("Expect").value_or(""));
    if (expect == "100-continue" && request_version_ >= "HTTP/1.1") {
      send_response_only(100);
      Flow f = end_headers();
      if (f != Flow::kOk) return f;
    }
    *ok = true;
    return Flow::kOk;
  }

  Flow write(const std::string& data) {
    if (data.empty()) return Flow::kOk;
    IoStatus st = s_.write_all(reinterpret_cast<const uint8_t*>(data.data()), data.size(), sock_timeout_);
    if (st.ok()) return Flow::kOk;
    if (st.kind == IoStatus::kTimeout) return Flow::kTimeout;
    fail_ = st;
    return Flow::kFail;
  }

  void send_response_only(int code, const std::string* message = nullptr) {
    if (request_version_ == "HTTP/0.9") return;
    std::string msg;
    if (message) {
      msg = *message;
    } else {
      const char* phrase = "";
      const char* explain = "";
      if (http::status_text(code, &phrase, &explain)) msg = phrase;
    }
    headers_out_ += "HTTP/1.1 " + std::to_string(code) + " " + msg + "\r\n";
  }

  void send_header(const std::string& key, const std::string& value) {
    if (request_version_ != "HTTP/0.9") headers_out_ += key + ": " + value + "\r\n";
    if (lower_ascii(key) == "connection") {
      std::string v = lower_ascii(value);
      if (v == "close") {
        close_connection_ = true;
      } else if (v == "keep-alive") {
        close_connection_ = false;
      }
    }
  }

  void send_response(int code, const std::string* message = nullptr) {
    send_response_only(code, message);
    send_header("Server", std::string("TheCrewReborn/") + kVersion);
    send_header("Date", http::http_date(plat::wall_time()));
  }

  Flow end_headers() {
    if (request_version_ == "HTTP/0.9") return Flow::kOk;
    headers_out_ += "\r\n";
    std::string out;
    out.swap(headers_out_);
    return write(out);
  }

  Flow send_error(int code, const std::string& message = std::string(), const std::string& explain = std::string()) {
    const char* phrase = "???";
    const char* long_text = "???";
    http::status_text(code, &phrase, &long_text);
    std::string msg = message.empty() ? std::string(phrase) : message;
    std::string expl = explain.empty() ? std::string(long_text) : explain;
    send_response(code, &msg);
    send_header("Connection", "close");
    std::string body;
    if (code >= 200 && code != 204 && code != 205 && code != 304) {
      body = tcr::latin1_to_utf8(
          "<!DOCTYPE HTML>\n"
          "<html lang=\"en\">\n"
          "    <head>\n"
          "        <meta charset=\"utf-8\">\n"
          "        <title>Error response</title>\n"
          "    </head>\n"
          "    <body>\n"
          "        <h1>Error response</h1>\n"
          "        <p>Error code: " +
          std::to_string(code) +
          "</p>\n"
          "        <p>Message: " +
          http::html_escape(msg) +
          ".</p>\n"
          "        <p>Error code explanation: " +
          std::to_string(code) + " - " + http::html_escape(expl) +
          ".</p>\n"
          "    </body>\n"
          "</html>\n");
      send_header("Content-Type", "text/html;charset=utf-8");
      send_header("Content-Length", std::to_string(body.size()));
    }
    Flow f = end_headers();
    if (f != Flow::kOk) return f;
    if (command_ != "HEAD" && !body.empty()) return write(body);
    return Flow::kOk;
  }

  void replying() {
    reader_.clear_deadline();
    sock_timeout_ = lim_.timeout;
  }

  void say(const std::string& text, bool verified) {
    if (!srv_.hooks.say) return;
    CoreLock lock;
    srv_.hooks.say(peer_.ip, text, verified);
  }

  std::string logged_path() const { return tcr::printable(std::string_view(path_).substr(0, path_.find('?'))); }

  Flow refuse(int status, const std::string& why) {
    replying();
    say("http " + peer_.ip + " " + tcr::latin1_to_utf8(command_) + " " + logged_path() + " -> " +
            std::to_string(status) + " (" + why + ")",
        false);
    std::string payload = "{\"error\": " + tcr::json_quote_utf8(why) + "}";
    close_connection_ = true;
    send_response(status);
    send_header("Content-Type", "application/json; charset=utf-8");
    send_header("Content-Length", std::to_string(payload.size()));
    send_header("Connection", "close");
    Flow f = end_headers();
    if (f != Flow::kOk) return f;
    return write(payload);
  }

  void drain(size_t n) {
    reader_.arm(lim_.drain_timeout);
    while (n > 0) {
      std::string chunk;
      if (reader_.read(std::min<size_t>(n, 65536), &chunk) != Flow::kOk) return;
      if (chunk.empty()) return;
      n -= chunk.size();
    }
  }

  Flow serve() {
    std::optional<std::string> cl = headers_.get("Content-Length");
    int64_t length = 0;
    if (cl && !cl->empty() && !http::py_int_latin1(*cl, &length)) length = -1;
    if (length < 0) return refuse(400, "bad Content-Length");
    if (static_cast<uint64_t>(length) > lim_.max_body) {
      Flow f = refuse(413, "body larger than " + std::to_string(lim_.max_body) + " bytes");
      if (f != Flow::kOk) return f;
      if (static_cast<uint64_t>(length) <= lim_.drain_limit) {
        drain(std::min<size_t>(static_cast<size_t>(length), lim_.max_body + 65536));
      }
      return Flow::kOk;
    }
    Request req;
    Flow f = reader_.read(static_cast<size_t>(length), &req.raw);
    if (f != Flow::kOk) return f;
    replying();
    req.method = command_;
    req.path = path_;
    req.version = request_version_;
    req.headers = headers_;
    req.auth = headers_.get("Authorization").value_or("");
    req.host = plat::local_addr(s_.socket()).ip;
    req.client_ip = peer_.ip;
    req.client_port = peer_.port;
    req.tls = tls_;
    Response resp;
    {
      CoreLock lock;
      if (srv_.hooks.route) resp = srv_.hooks.route(req);
    }
    if (resp.raised) {
      fail_ = IoStatus::raised(resp.exception);
      return Flow::kFail;
    }
    std::string base(std::string_view(path_).substr(0, path_.find('?')));
    std::string trimmed = base;
    while (!trimmed.empty() && trimmed.back() == '/') trimmed.pop_back();
    bool login = command_ == "POST" && resp.status == 200 && ends_with(trimmed, "/profiles/sessions");
    bool verified = login;
    if (!verified && srv_.hooks.live_ticket) {
      std::string raised;
      {
        CoreLock lock;
        verified = srv_.hooks.live_ticket(req.auth, &raised);
      }
      if (!raised.empty()) {
        fail_ = IoStatus::raised(raised);
        return Flow::kFail;
      }
    }
    say("http " + peer_.ip + " " + tcr::latin1_to_utf8(command_) + " " + logged_path() + " -> " +
            std::to_string(resp.status),
        verified);
    if (!srv_.log_path.empty()) {
      std::string reply_json = resp.log_reply;
      if (resp.binary) {
        reply_json = tcr::json_quote_utf8("<png " + std::to_string(resp.payload.size()) + " bytes>");
      } else if (reply_json.empty()) {
        reply_json = resp.payload;
      }
      std::string line =
          capture::request_line(peer_.ip, command_, path_, req.auth, req.raw, resp.status, reply_json);
      std::string why;
      bool ok;
      {
        plat::Lock lock(g_log_lock);
        ok = capture::append_log(srv_.log_path, line, &why);
      }
      if (!ok) capture::log_failed(srv_.log_path, why);
    }
    send_response(resp.status);
    std::string kind = ends_with(path_, ".rgba") ? "application/octet-stream" : "image/png";
    if (!resp.content_type.empty()) {
      send_header("Content-Type", resp.content_type);
    } else {
      send_header("Content-Type", resp.binary ? kind : "application/json; charset=utf-8");
    }
    send_header("Content-Length", std::to_string(resp.payload.size()));
    for (const auto& h : resp.headers) send_header(h.first, h.second);
    f = end_headers();
    if (f != Flow::kOk) return f;
    return write(resp.payload);
  }

  HttpServer::Shared& srv_;
  const Limits& lim_;
  Stream& s_;
  plat::Addr peer_;
  bool tls_;
  double sock_timeout_;
  Reader reader_;
  bool close_connection_ = true;
  int requests_ = 0;
  std::string requestline_, command_, request_version_, path_;
  http::Headers headers_;
  std::string headers_out_;
  IoStatus fail_;
};

void handle_error(HttpServer::Shared& srv, const plat::Addr& peer, const IoStatus& st) {
  if (st.kind == IoStatus::kSslError) {
    if (srv.hooks.say) {
      CoreLock lock;
      srv.hooks.say(peer.ip, "http " + peer.ip + " " + st.ssl_type + " " + st.ssl_reason, false);
    }
    return;
  }
  if (st.kind == IoStatus::kException) {
    plat::print_error_line("----------------------------------------");
    plat::print_error_line("Exception occurred during processing of request from ('" + peer.ip + "', " +
                           std::to_string(peer.port) + ")");
    plat::print_error_line("Traceback (most recent call last):");
    plat::print_error_line(st.exception);
    plat::print_error_line("----------------------------------------");
    return;
  }
  if (st.kind != IoStatus::kOsError) return;
  plat::ErrorKind kind = plat::error_kind(st.os);
  if (kind == plat::ErrorKind::kReset || kind == plat::ErrorKind::kBrokenPipe || kind == plat::ErrorKind::kAborted ||
      kind == plat::ErrorKind::kRefused || kind == plat::ErrorKind::kTimeout) {
    std::string text = "http " + peer.ip + " " + plat::python_error_name(st.os) + " " + st.os.text;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    if (srv.hooks.say) {
      CoreLock lock;
      srv.hooks.say(peer.ip, text, false);
    }
    return;
  }
  plat::print_error_line("----------------------------------------");
  plat::print_error_line("Exception occurred during processing of request from ('" + peer.ip + "', " +
                         std::to_string(peer.port) + ")");
  plat::print_error_line("Traceback (most recent call last):");
  plat::print_error_line(std::string(plat::python_error_name(st.os)) + ": " + plat::python_error_text(st.os));
  plat::print_error_line("----------------------------------------");
}

struct ConnStart {
  std::shared_ptr<HttpServer::Shared> srv;
  plat::Socket sock;
  plat::Addr peer;
};

void serve_connection(void* p) {
  std::unique_ptr<ConnStart> start(static_cast<ConnStart*>(p));
  HttpServer::Shared& srv = *start->srv;
  {
    std::unique_ptr<Stream> stream;
    if (srv.tls) {
      stream = srv.tls->wrap(start->sock);
    } else {
      stream.reset(new PlainStream(start->sock));
    }
    if (stream) {
      Handler h(srv, *stream, start->peer, srv.tls != nullptr);
      if (h.handle() == Flow::kFail) handle_error(srv, start->peer, h.failure());
    }
  }
  srv.release(start->peer.ip);
  plat::shutdown_write(start->sock);
  plat::close_socket(start->sock);
}

void accept_loop(void* p) {
  std::shared_ptr<HttpServer::Shared> srv = *static_cast<std::shared_ptr<HttpServer::Shared>*>(p);
  delete static_cast<std::shared_ptr<HttpServer::Shared>*>(p);
  while (srv->running.load()) {
    plat::Addr peer;
    plat::Error err;
    plat::Socket s = plat::tcp_accept(srv->listener, 0.5, &peer, &err);
    if (s == plat::kNoSocket) {
      if (err.code != 0) plat::sleep_seconds(0.01);
      continue;
    }
    bool refuse;
    {
      plat::Lock lock(srv->conn_lock);
      refuse = srv->open >= srv->limits.max_conn || srv->per_ip[peer.ip] >= srv->limits.max_conn_per_ip;
      if (srv->per_ip[peer.ip] == 0 && refuse) srv->per_ip.erase(peer.ip);
      if (!refuse) {
        ++srv->open;
        ++srv->per_ip[peer.ip];
      }
    }
    if (refuse) {
      plat::shutdown_write(s);
      plat::close_socket(s);
      continue;
    }
    ConnStart* start = new ConnStart{srv, s, peer};
    if (!plat::start_thread(serve_connection, start)) {
      delete start;
      srv->release(peer.ip);
      plat::shutdown_write(s);
      plat::close_socket(s);
    }
  }
  srv->accept_done.store(true);
}

}

HttpServer::HttpServer(Hooks hooks, Limits limits) : shared_(std::make_shared<Shared>()) {
  shared_->hooks = std::move(hooks);
  shared_->limits = limits;
}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start(const std::string& ip, int port, tls::ServerContext* tls, const std::string& log_path,
                       plat::Error* err) {
  plat::Socket s = plat::tcp_listen(ip.c_str(), port, kBacklog, true, err);
  if (s == plat::kNoSocket) return false;
  shared_->listener = s;
  shared_->tls = tls;
  shared_->log_path = log_path;
  port_ = plat::local_addr(s).port;
  shared_->running.store(true);
  shared_->accept_done.store(false);
  auto* arg = new std::shared_ptr<Shared>(shared_);
  if (!plat::start_thread(accept_loop, arg)) {
    delete arg;
    shared_->running.store(false);
    shared_->accept_done.store(true);
    plat::close_socket(s);
    shared_->listener = plat::kNoSocket;
    if (err) err->text = "cannot start a thread";
    return false;
  }
  return true;
}

int HttpServer::open_connections() {
  plat::Lock lock(shared_->conn_lock);
  return shared_->open;
}

void HttpServer::stop() {
  if (!shared_->running.exchange(false)) return;
  while (!shared_->accept_done.load()) plat::sleep_seconds(0.01);
  plat::close_socket(shared_->listener);
  shared_->listener = plat::kNoSocket;
}

}
