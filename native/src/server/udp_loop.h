#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "platform/platform.h"

namespace tcr::server {

struct Datagram {
  std::string data;
  plat::Addr to;
};

class UdpLoop {
 public:
  using OnDatagram = std::function<std::vector<Datagram>(const std::string& data, const plat::Addr& from)>;
  using OnSendFailed = std::function<void(const plat::Addr& to, const std::string& error)>;
  using OnDone = std::function<void(size_t packets)>;

  UdpLoop() = default;
  ~UdpLoop();
  UdpLoop(const UdpLoop&) = delete;
  UdpLoop& operator=(const UdpLoop&) = delete;

  bool open(const std::string& ip, int port, bool broadcast, plat::Error* err);
  int port() const { return port_; }
  bool start(size_t max_datagram, OnDatagram on_datagram, OnSendFailed on_send_failed, OnDone on_done = nullptr);
  void serve(size_t max_datagram, const OnDatagram& on_datagram, const OnSendFailed& on_send_failed,
             const OnDone& on_done = nullptr);
  bool send(const Datagram& d, plat::Error* err);
  void close();

  struct State;

 private:
  int port_ = 0;
  std::shared_ptr<State> state_;
};

}
