#include "core/pymath.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>

namespace tcr {
namespace py {

namespace {

struct DoubleLength {
  double hi;
  double lo;
};

DoubleLength dl_mul(double x, double y) {
  double z = x * y;
  double zz = std::fma(x, y, -z);
  return {z, zz};
}

DoubleLength dl_fast_sum(double a, double b) {
  double x = a + b;
  double y = (a - x) + b;
  return {x, y};
}

double vector_norm(size_t n, double* vec, double max, bool found_nan) {
  if (std::isinf(max)) return max;
  if (found_nan) return std::numeric_limits<double>::quiet_NaN();
  if (max == 0.0 || n <= 1) return max;
  int max_e = 0;
  std::frexp(max, &max_e);
  if (max_e < -1023) {
    for (size_t i = 0; i < n; i++) vec[i] /= DBL_MIN;
    return DBL_MIN * vector_norm(n, vec, max / DBL_MIN, found_nan);
  }
  double scale = std::ldexp(1.0, -max_e);
  double csum = 1.0, frac1 = 0.0, frac2 = 0.0;
  DoubleLength pr, sm;
  for (size_t i = 0; i < n; i++) {
    double x = vec[i] * scale;
    pr = dl_mul(x, x);
    sm = dl_fast_sum(csum, pr.hi);
    csum = sm.hi;
    frac1 += pr.lo;
    frac2 += sm.lo;
  }
  double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
  pr = dl_mul(-h, h);
  sm = dl_fast_sum(csum, pr.hi);
  csum = sm.hi;
  frac1 += pr.lo;
  frac2 += sm.lo;
  double x = csum - 1.0 + (frac1 + frac2);
  h += x / (2.0 * h);
  return h / scale;
}

void append_unit(std::string& out, uint32_t cp) { append_utf8(out, cp); }

}

double hypot(double x, double y) {
  double vec[2] = {std::fabs(x), std::fabs(y)};
  double max = 0.0;
  bool found_nan = false;
  for (double v : vec) {
    found_nan |= std::isnan(v);
    if (v > max) max = v;
  }
  return vector_norm(2, vec, max, found_nan);
}

bool pack_f32(Bytes& out, double x, Raised* raised) {
  float y = static_cast<float>(x);
  if (std::isinf(y) && !std::isinf(x)) {
    *raised = Raised{"OverflowError", "float too large to pack with f format"};
    return false;
  }
  uint32_t bits = 0;
  std::memcpy(&bits, &y, sizeof(bits));
  out.push_back(static_cast<uint8_t>(bits >> 24));
  out.push_back(static_cast<uint8_t>(bits >> 16));
  out.push_back(static_cast<uint8_t>(bits >> 8));
  out.push_back(static_cast<uint8_t>(bits));
  return true;
}

std::string decode_utf16le_replace(const uint8_t* data, size_t n) {
  std::string out;
  size_t q = 0;
  while (q < n) {
    if (n - q < 2) {
      append_unit(out, 0xFFFD);
      break;
    }
    uint32_t ch = data[q] | (uint32_t(data[q + 1]) << 8);
    q += 2;
    if (ch < 0xD800 || ch > 0xDFFF) {
      append_unit(out, ch);
      continue;
    }
    if (ch >= 0xDC00) {
      append_unit(out, 0xFFFD);
      continue;
    }
    if (n - q < 2) {
      append_unit(out, 0xFFFD);
      break;
    }
    uint32_t ch2 = data[q] | (uint32_t(data[q + 1]) << 8);
    if (ch2 < 0xDC00 || ch2 > 0xDFFF) {
      append_unit(out, 0xFFFD);
      continue;
    }
    q += 2;
    append_unit(out, 0x10000 + (((ch & 0x3FF) << 10) | (ch2 & 0x3FF)));
  }
  return out;
}

Bytes encode_utf16le(const std::string& text) {
  Bytes out;
  auto unit = [&](uint32_t u) {
    out.push_back(static_cast<uint8_t>(u));
    out.push_back(static_cast<uint8_t>(u >> 8));
  };
  for (uint32_t cp : code_points(text)) {
    if (cp >= 0x10000) {
      cp -= 0x10000;
      unit(0xD800 | (cp >> 10));
      unit(0xDC00 | (cp & 0x3FF));
    } else {
      unit(cp);
    }
  }
  return out;
}

}
}
