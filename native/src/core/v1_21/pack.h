#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/accounts.h"
#include "core/json.h"
#include "core/pyobj.h"
#include "core/util.h"

namespace tcr {
namespace v1_21 {
namespace pack {

double f32_of_bits(uint32_t bits);
double f32_at(const Bytes& b, size_t p);

struct Num {
  bool is_int = true;
  bool negative = false;
  uint64_t magnitude = 0;
  bool big = false;

  static Num of(uint64_t u) { return Num{true, false, u, false}; }
  static Num of_signed(int64_t i) {
    return i < 0 ? Num{true, true, static_cast<uint64_t>(-(i + 1)) + 1, false} : Num{true, false, uint64_t(i), false};
  }
  static Num not_int() { return Num{false, false, 0, false}; }
  static Num of(const json::Value& v);
};

inline Raised error(std::string text) { return Raised{"struct.error", std::move(text)}; }

bool put(Bytes& out, char code, const Num& v, Raised* raised);
bool unpack_check(const Bytes& b, size_t offset, size_t size, Raised* raised);
bool unpack_u32(const Bytes& b, size_t offset, uint32_t* out, Raised* raised);
std::optional<uint64_t> u64_of(const json::Value& v, Raised* raised);

std::string exception_repr(const Raised& r);

std::optional<Bytes> u64_array(obj::Py& py, const json::Value& words);

class Packer {
 public:
  Packer& B(const json::Value& v) { return put('B', v); }
  Packer& H(const json::Value& v) { return put('H', v); }
  Packer& I(const json::Value& v) { return put('I', v); }
  Packer& Q(const json::Value& v) { return put('Q', v); }
  Packer& B(int64_t v) { return put('B', json::Value::integer(v)); }
  Packer& H(int64_t v) { return put('H', json::Value::integer(v)); }
  Packer& I(int64_t v) { return put('I', json::Value::integer(v)); }
  Packer& Q(uint64_t v) { return put('Q', json::Value::uinteger(v)); }
  Packer& Qs(int64_t v) { return put('Q', json::Value::integer(v)); }
  Packer& q(int64_t v) { return put('q', json::Value::integer(v)); }
  Packer& raw(const Bytes& b) {
    if (!failed_) out_.insert(out_.end(), b.begin(), b.end());
    return *this;
  }
  bool failed() const { return failed_; }
  const Raised& raised() const { return raised_; }
  Bytes& out() { return out_; }

 private:
  Packer& put(char code, const json::Value& v);
  Bytes out_;
  Raised raised_;
  bool failed_ = false;
};

}
}
}
