#include "core/v1_21/pack.h"

#include <cstring>

#include "core/pyobj.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {
namespace pack {

double f32_of_bits(uint32_t bits) {
  float f;
  static_assert(sizeof(f) == sizeof(bits), "float is 32 bits");
  std::memcpy(&f, &bits, sizeof(f));
  return static_cast<double>(f);
}

double f32_at(const Bytes& b, size_t p) { return f32_of_bits(wire::get_u32(b.data() + p)); }

Num Num::of(const json::Value& v) {
  switch (v.type()) {
    case json::Type::Bool:
      return Num::of(uint64_t(v.as_bool() ? 1 : 0));
    case json::Type::Int:
      return Num::of_signed(*v.as_i64());
    case json::Type::BigInt: {
      std::optional<uint64_t> u = v.as_u64();
      if (u) return Num::of(*u);
      Num n;
      n.negative = v.int_text()[0] == '-';
      n.big = true;
      return n;
    }
    default:
      return Num::not_int();
  }
}

bool put(Bytes& out, char code, const Num& v, Raised* raised) {
  if (!v.is_int) {
    *raised = error("required argument is not an integer");
    return false;
  }
  uint64_t max = code == 'B' ? 0xFF : code == 'H' ? 0xFFFF : code == 'I' ? 0xFFFFFFFFull : ~uint64_t(0);
  if (v.negative || v.big || v.magnitude > max) {
    *raised = error(std::string("'") + code + "' format requires 0 <= number <= " + py::uint_to_text(max));
    return false;
  }
  switch (code) {
    case 'B':
      out.push_back(static_cast<uint8_t>(v.magnitude));
      break;
    case 'H':
      wire::put_u16(out, static_cast<uint16_t>(v.magnitude));
      break;
    case 'I':
      wire::put_u32(out, static_cast<uint32_t>(v.magnitude));
      break;
    default:
      wire::put_u64(out, v.magnitude);
  }
  return true;
}

bool unpack_check(const Bytes& b, size_t offset, size_t size, Raised* raised) {
  if (offset + size <= b.size()) return true;
  *raised = error("unpack_from requires a buffer of at least " + py::uint_to_text(offset + size) +
                  " bytes for unpacking " + py::uint_to_text(size) + " bytes at offset " + py::uint_to_text(offset) +
                  " (actual buffer size is " + py::uint_to_text(b.size()) + ")");
  return false;
}

bool unpack_u32(const Bytes& b, size_t offset, uint32_t* out, Raised* raised) {
  if (!unpack_check(b, offset, 4, raised)) return false;
  *out = wire::get_u32(b.data() + offset);
  return true;
}

std::optional<uint64_t> u64_of(const json::Value& v, Raised* raised) {
  Bytes scratch;
  Num n = Num::of(v);
  if (!put(scratch, 'Q', n, raised)) return std::nullopt;
  return n.magnitude;
}

std::optional<Bytes> u64_array(obj::Py& py, const json::Value& words) {
  size_t n = py.len(words);
  Bytes body;
  wire::put_u32(body, static_cast<uint32_t>(n));
  for (const json::Value& w : py.iter(words)) {
    if (!py.pack(body, 'Q', w)) return std::nullopt;
  }
  if (py.failed()) return std::nullopt;
  return wire::blob(wire::frame(wire::T_ARRAY, body));
}

Packer& Packer::put(char code, const json::Value& v) {
  if (failed_) return *this;
  obj::Py py;
  if (!py.pack(out_, code, v)) {
    failed_ = true;
    raised_ = py.raised();
  }
  return *this;
}

std::string exception_repr(const Raised& r) {
  std::string cls = r.type;
  size_t dot = cls.rfind('.');
  if (dot != std::string::npos) cls = cls.substr(dot + 1);
  return cls + "(" + py::repr(r.text) + ")";
}

}
}
}
