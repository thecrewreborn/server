#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/accounts.h"
#include "core/util.h"

namespace tcr {
namespace v1_00 {
namespace translate {

struct Value {
  enum Kind { kNone, kInt, kBytes } kind = kNone;
  uint64_t num = 0;
  Bytes bytes;

  static Value none() { return Value(); }
  static Value of(uint64_t v) {
    Value x;
    x.kind = kInt;
    x.num = v;
    return x;
  }
  static Value of(Bytes b) {
    Value x;
    x.kind = kBytes;
    x.bytes = std::move(b);
    return x;
  }
};

using Mapping = std::vector<std::optional<size_t>>;

std::vector<std::string> expand(const std::vector<std::string>& layout);
Mapping align(const std::vector<std::string>& layout121, const std::vector<std::string>& layout100);
bool decode(const std::vector<std::string>& tokens, const Bytes& body, std::vector<Value>* out, Raised* raised);
bool encode(const std::vector<std::string>& tokens, const std::vector<Value>& values, Bytes* out, Raised* raised);
bool transcode(const Bytes& body121, const std::vector<std::string>& layout121,
               const std::vector<std::string>& layout100, const Mapping* mapping, Bytes* out, Raised* raised);

}
}
}
