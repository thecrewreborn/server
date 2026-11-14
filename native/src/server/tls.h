#pragma once

#include <memory>
#include <string>

#include "platform/platform.h"
#include "server/stream.h"

namespace tcr::tls {

extern const char* const kNames[2];

bool ensure(const std::string& cert_path, const std::string& key_path, const std::string& ip, bool* wrote,
            std::string* why, plat::Error* os_error = nullptr, std::string* os_path = nullptr);

class ServerContext {
 public:
  ServerContext();
  ~ServerContext();
  ServerContext(const ServerContext&) = delete;
  ServerContext& operator=(const ServerContext&) = delete;

  bool load(const std::string& cert_path, const std::string& key_path, std::string* why,
            plat::Error* os_error = nullptr);

  std::unique_ptr<server::Stream> wrap(plat::Socket s);

  struct Impl;

 private:
  Impl* impl_;
};

}
