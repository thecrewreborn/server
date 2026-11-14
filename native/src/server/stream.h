#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "platform/platform.h"

namespace tcr::server {

struct IoStatus {
  enum Kind { kOk, kTimeout, kOsError, kSslError, kException };
  Kind kind = kOk;
  plat::Error os;
  std::string ssl_type;
  std::string ssl_reason;
  std::string exception;

  bool ok() const { return kind == kOk; }
  static IoStatus timeout() {
    IoStatus s;
    s.kind = kTimeout;
    return s;
  }
  static IoStatus raised(const std::string& line) {
    IoStatus s;
    s.kind = kException;
    s.exception = line;
    return s;
  }
  static IoStatus os_error(const plat::Error& e) {
    IoStatus s;
    s.kind = kOsError;
    s.os = e;
    return s;
  }
};

class Stream {
 public:
  virtual ~Stream() = default;
  virtual IoStatus read(uint8_t* buf, size_t n, double timeout, size_t* got) = 0;
  virtual IoStatus write_all(const uint8_t* data, size_t n, double timeout) = 0;
  virtual plat::Socket socket() const = 0;
};

class PlainStream : public Stream {
 public:
  explicit PlainStream(plat::Socket s) : sock_(s) {}
  IoStatus read(uint8_t* buf, size_t n, double timeout, size_t* got) override;
  IoStatus write_all(const uint8_t* data, size_t n, double timeout) override;
  plat::Socket socket() const override { return sock_; }

 private:
  plat::Socket sock_;
};

}
