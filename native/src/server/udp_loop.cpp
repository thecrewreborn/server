#include "server/udp_loop.h"

#include <utility>

#include "server/core_lock.h"

namespace tcr::server {

struct UdpLoop::State {
  explicit State(plat::Socket s) : sock(s) {}
  ~State() { plat::close_socket(sock); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;

  const plat::Socket sock;
  std::atomic<bool> closed{false};
  std::atomic<int> serving{0};
};

UdpLoop::~UdpLoop() { close(); }

bool UdpLoop::open(const std::string& ip, int port, bool broadcast, plat::Error* err) {
  close();
  plat::Socket sock = plat::udp_open(ip.c_str(), port, true, broadcast, err);
  if (sock == plat::kNoSocket) return false;
  port_ = plat::local_addr(sock).port;
  state_ = std::make_shared<State>(sock);
  return true;
}

namespace {

void serve_socket(const std::shared_ptr<UdpLoop::State>& state, size_t max_datagram,
                  const UdpLoop::OnDatagram& on_datagram, const UdpLoop::OnSendFailed& on_send_failed,
                  const UdpLoop::OnDone& on_done) {
  plat::Socket sock = state->sock;
  std::string buf(max_datagram, '\0');
  while (!state->closed.load()) {
    plat::Error err;
    if (plat::wait_readable(sock, 0.5, &err) <= 0) continue;
    if (state->closed.load()) break;
    plat::Addr from;
    long n = plat::udp_recvfrom(sock, &buf[0], buf.size(), &from, &err);
    if (n < 0) continue;
    std::vector<Datagram> out;
    {
      CoreLock lock;
      out = on_datagram(std::string(buf.data(), static_cast<size_t>(n)), from);
    }
    bool failed = false;
    for (const Datagram& d : out) {
      plat::Error send_err;
      if (plat::udp_sendto(sock, d.data.data(), d.data.size(), d.to, &send_err) < 0 && !failed) {
        failed = true;
        if (on_send_failed) {
          CoreLock lock;
          on_send_failed(d.to, plat::python_error_text(send_err));
        }
      }
    }
    if (on_done) on_done(out.size());
  }
  state->serving.fetch_sub(1);
}

}

void UdpLoop::serve(size_t max_datagram, const OnDatagram& on_datagram, const OnSendFailed& on_send_failed,
                    const OnDone& on_done) {
  std::shared_ptr<State> state = state_;
  if (!state) return;
  state->serving.fetch_add(1);
  serve_socket(state, max_datagram, on_datagram, on_send_failed, on_done);
}

bool UdpLoop::start(size_t max_datagram, OnDatagram on_datagram, OnSendFailed on_send_failed, OnDone on_done) {
  std::shared_ptr<State> state = state_;
  if (!state) return false;
  state->serving.fetch_add(1);
  bool ok = plat::start_thread([state, max_datagram, on_datagram = std::move(on_datagram),
                                on_send_failed = std::move(on_send_failed), on_done = std::move(on_done)]() {
    serve_socket(state, max_datagram, on_datagram, on_send_failed, on_done);
  });
  if (!ok) state->serving.fetch_sub(1);
  return ok;
}

bool UdpLoop::send(const Datagram& d, plat::Error* err) {
  std::shared_ptr<State> state = state_;
  if (!state || state->closed.load()) {
#if defined(_WIN32)
    if (err) *err = plat::system_error(10038);
#else
    if (err) *err = plat::system_error(9);
#endif
    return false;
  }
  return plat::udp_sendto(state->sock, d.data.data(), d.data.size(), d.to, err) >= 0;
}

void UdpLoop::close() {
  std::shared_ptr<State> state = std::move(state_);
  state_.reset();
  if (!state) return;
  state->closed.store(true);
  if (holds_core_lock()) return;
  while (state->serving.load() > 0) plat::sleep_seconds(0.01);
}

}
