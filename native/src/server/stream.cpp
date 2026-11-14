#include "server/stream.h"

namespace tcr::server {

namespace {
double remaining(double deadline) {
  if (deadline < 0) return -1;
  double left = deadline - plat::monotonic();
  return left < 0 ? 0 : left;
}
}

IoStatus PlainStream::read(uint8_t* buf, size_t n, double timeout, size_t* got) {
  *got = 0;
  double deadline = timeout >= 0 ? plat::monotonic() + timeout : -1;
  for (;;) {
    plat::Error err;
    int w = plat::wait_readable(sock_, remaining(deadline), &err);
    if (w < 0) return IoStatus::os_error(err);
    if (w == 0) return IoStatus::timeout();
    long r = plat::sock_recv(sock_, buf, n, &err);
    if (r >= 0) {
      *got = static_cast<size_t>(r);
      return IoStatus();
    }
    if (r != plat::kWouldBlock) return IoStatus::os_error(err);
  }
}

IoStatus PlainStream::write_all(const uint8_t* data, size_t n, double timeout) {
  double deadline = timeout >= 0 ? plat::monotonic() + timeout : -1;
  while (n > 0) {
    plat::Error err;
    int w = plat::wait_writable(sock_, remaining(deadline), &err);
    if (w < 0) return IoStatus::os_error(err);
    if (w == 0) return IoStatus::timeout();
    long r = plat::sock_send(sock_, data, n, &err);
    if (r == plat::kWouldBlock) continue;
    if (r < 0) return IoStatus::os_error(err);
    data += r;
    n -= static_cast<size_t>(r);
  }
  return IoStatus();
}

}
