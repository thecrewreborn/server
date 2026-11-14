#include "core/bigint.h"

#include <algorithm>

namespace tcr {
namespace py {

void BigInt::trim() {
  while (!mag_.empty() && mag_.back() == 0) mag_.pop_back();
  if (mag_.empty()) neg_ = false;
}

BigInt BigInt::of_unsigned(uint64_t v) {
  BigInt b;
  while (v) {
    b.mag_.push_back(static_cast<uint32_t>(v));
    v >>= 32;
  }
  return b;
}

BigInt BigInt::of(int64_t v) {
  if (v >= 0) return of_unsigned(static_cast<uint64_t>(v));
  BigInt b = of_unsigned(static_cast<uint64_t>(-(v + 1)) + 1);
  b.neg_ = true;
  return b;
}

std::optional<BigInt> BigInt::parse(std::string_view decimal) {
  BigInt b;
  size_t i = 0;
  bool neg = false;
  if (i < decimal.size() && decimal[i] == '-') {
    neg = true;
    i++;
  }
  if (i == decimal.size()) return std::nullopt;
  for (; i < decimal.size(); i++) {
    char c = decimal[i];
    if (c < '0' || c > '9') return std::nullopt;
    uint64_t carry = static_cast<uint64_t>(c - '0');
    for (uint32_t& limb : b.mag_) {
      uint64_t x = static_cast<uint64_t>(limb) * 10 + carry;
      limb = static_cast<uint32_t>(x);
      carry = x >> 32;
    }
    if (carry) b.mag_.push_back(static_cast<uint32_t>(carry));
  }
  b.neg_ = neg;
  b.trim();
  return b;
}

std::string BigInt::text() const {
  if (mag_.empty()) return "0";
  std::vector<uint32_t> m = mag_;
  std::string out;
  while (!m.empty()) {
    uint64_t rem = 0;
    for (size_t k = m.size(); k-- > 0;) {
      uint64_t cur = (rem << 32) | m[k];
      m[k] = static_cast<uint32_t>(cur / 1000000000u);
      rem = cur % 1000000000u;
    }
    while (!m.empty() && m.back() == 0) m.pop_back();
    for (int d = 0; d < 9; d++) {
      out.push_back(static_cast<char>('0' + rem % 10));
      rem /= 10;
      if (m.empty() && rem == 0) break;
    }
  }
  while (out.size() > 1 && out.back() == '0') out.pop_back();
  if (neg_) out.push_back('-');
  std::reverse(out.begin(), out.end());
  return out;
}

std::optional<int64_t> BigInt::to_i64() const {
  if (mag_.size() > 2) return std::nullopt;
  uint64_t m = 0;
  for (size_t k = mag_.size(); k-- > 0;) m = (m << 32) | mag_[k];
  if (!neg_) {
    if (m > static_cast<uint64_t>(INT64_MAX)) return std::nullopt;
    return static_cast<int64_t>(m);
  }
  if (m > static_cast<uint64_t>(INT64_MAX) + 1) return std::nullopt;
  if (m == static_cast<uint64_t>(INT64_MAX) + 1) return INT64_MIN;
  return -static_cast<int64_t>(m);
}

size_t BigInt::bit_length() const {
  if (mag_.empty()) return 0;
  size_t n = (mag_.size() - 1) * 32;
  uint32_t top = mag_.back();
  while (top) {
    n++;
    top >>= 1;
  }
  return n;
}

int BigInt::compare_mag(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  for (size_t k = a.size(); k-- > 0;) {
    if (a[k] != b[k]) return a[k] < b[k] ? -1 : 1;
  }
  return 0;
}

int BigInt::compare(const BigInt& o) const {
  if (neg_ != o.neg_) return neg_ ? -1 : 1;
  int c = compare_mag(mag_, o.mag_);
  return neg_ ? -c : c;
}

std::vector<uint32_t> BigInt::add_mag(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
  std::vector<uint32_t> out;
  uint64_t carry = 0;
  for (size_t k = 0; k < std::max(a.size(), b.size()); k++) {
    uint64_t x = carry + (k < a.size() ? a[k] : 0) + (k < b.size() ? b[k] : 0);
    out.push_back(static_cast<uint32_t>(x));
    carry = x >> 32;
  }
  if (carry) out.push_back(static_cast<uint32_t>(carry));
  return out;
}

std::vector<uint32_t> BigInt::sub_mag(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
  std::vector<uint32_t> out;
  int64_t borrow = 0;
  for (size_t k = 0; k < a.size(); k++) {
    int64_t x = static_cast<int64_t>(a[k]) - borrow - (k < b.size() ? static_cast<int64_t>(b[k]) : 0);
    borrow = x < 0 ? 1 : 0;
    if (x < 0) x += int64_t(1) << 32;
    out.push_back(static_cast<uint32_t>(x));
  }
  while (!out.empty() && out.back() == 0) out.pop_back();
  return out;
}

BigInt BigInt::operator-() const {
  BigInt r = *this;
  if (!r.mag_.empty()) r.neg_ = !r.neg_;
  return r;
}

BigInt BigInt::operator+(const BigInt& o) const {
  BigInt r;
  if (neg_ == o.neg_) {
    r.mag_ = add_mag(mag_, o.mag_);
    r.neg_ = neg_;
  } else if (compare_mag(mag_, o.mag_) >= 0) {
    r.mag_ = sub_mag(mag_, o.mag_);
    r.neg_ = neg_;
  } else {
    r.mag_ = sub_mag(o.mag_, mag_);
    r.neg_ = o.neg_;
  }
  r.trim();
  return r;
}

BigInt BigInt::operator-(const BigInt& o) const { return *this + (-o); }

BigInt BigInt::operator*(const BigInt& o) const {
  BigInt r;
  if (mag_.empty() || o.mag_.empty()) return r;
  r.mag_.assign(mag_.size() + o.mag_.size(), 0);
  for (size_t i = 0; i < mag_.size(); i++) {
    uint64_t carry = 0;
    for (size_t j = 0; j < o.mag_.size(); j++) {
      uint64_t x = static_cast<uint64_t>(mag_[i]) * o.mag_[j] + r.mag_[i + j] + carry;
      r.mag_[i + j] = static_cast<uint32_t>(x);
      carry = x >> 32;
    }
    size_t k = i + o.mag_.size();
    while (carry) {
      uint64_t x = static_cast<uint64_t>(r.mag_[k]) + carry;
      r.mag_[k++] = static_cast<uint32_t>(x);
      carry = x >> 32;
    }
  }
  r.neg_ = neg_ != o.neg_;
  r.trim();
  return r;
}

void BigInt::divmod(const BigInt& a, const BigInt& b, BigInt* q, BigInt* r) {
  BigInt quo, rem;
  size_t bits = a.bit_length();
  quo.mag_.assign(a.mag_.size(), 0);
  for (size_t i = bits; i-- > 0;) {
    rem = rem.shift_left(1);
    if (a.mag_[i / 32] >> (i % 32) & 1) {
      if (rem.mag_.empty()) rem.mag_.push_back(0);
      rem.mag_[0] |= 1;
    }
    if (compare_mag(rem.mag_, b.mag_) >= 0) {
      rem.mag_ = sub_mag(rem.mag_, b.mag_);
      quo.mag_[i / 32] |= uint32_t(1) << (i % 32);
    }
  }
  quo.neg_ = a.neg_ != b.neg_;
  quo.trim();
  rem.neg_ = a.neg_;
  rem.trim();
  if (!rem.zero() && rem.neg_ != b.neg_) {
    quo = quo - of(1);
    rem = rem + b;
  }
  *q = quo;
  *r = rem;
}

BigInt BigInt::shift_left(uint64_t n) const {
  BigInt r;
  if (mag_.empty()) return r;
  size_t limbs = static_cast<size_t>(n / 32), bits = static_cast<size_t>(n % 32);
  r.mag_.assign(limbs, 0);
  uint32_t carry = 0;
  for (uint32_t m : mag_) {
    r.mag_.push_back(bits ? (m << bits) | carry : m);
    carry = bits ? m >> (32 - bits) : 0;
  }
  if (carry) r.mag_.push_back(carry);
  r.neg_ = neg_;
  r.trim();
  return r;
}

BigInt BigInt::shift_right(uint64_t n) const {
  if (neg_) {
    BigInt t = (-*this) - of(1);
    return -(t.shift_right(n)) - of(1);
  }
  BigInt r;
  size_t limbs = static_cast<size_t>(n / 32), bits = static_cast<size_t>(n % 32);
  if (n / 32 >= mag_.size()) return r;
  for (size_t k = limbs; k < mag_.size(); k++) {
    uint32_t lo = mag_[k] >> bits;
    uint32_t hi = bits && k + 1 < mag_.size() ? mag_[k + 1] << (32 - bits) : 0;
    r.mag_.push_back(lo | hi);
  }
  r.trim();
  return r;
}

std::vector<uint32_t> BigInt::twos(size_t n) const {
  std::vector<uint32_t> t(n, 0);
  for (size_t k = 0; k < mag_.size() && k < n; k++) t[k] = mag_[k];
  if (neg_) {
    uint64_t carry = 1;
    for (uint32_t& x : t) {
      uint64_t v = static_cast<uint64_t>(static_cast<uint32_t>(~x)) + carry;
      x = static_cast<uint32_t>(v);
      carry = v >> 32;
    }
  }
  return t;
}

BigInt BigInt::from_twos(const std::vector<uint32_t>& t) {
  BigInt r;
  bool neg = !t.empty() && (t.back() >> 31);
  if (!neg) {
    r.mag_ = t;
    r.trim();
    return r;
  }
  std::vector<uint32_t> m(t.size());
  uint64_t carry = 1;
  for (size_t k = 0; k < t.size(); k++) {
    uint64_t v = static_cast<uint64_t>(static_cast<uint32_t>(~t[k])) + carry;
    m[k] = static_cast<uint32_t>(v);
    carry = v >> 32;
  }
  r.mag_ = m;
  r.neg_ = true;
  r.trim();
  return r;
}

BigInt BigInt::bitwise(const BigInt& o, int op) const {
  size_t n = std::max(mag_.size(), o.mag_.size()) + 1;
  std::vector<uint32_t> a = twos(n), b = o.twos(n), out(n);
  for (size_t k = 0; k < n; k++) out[k] = op == 0 ? a[k] & b[k] : op == 1 ? a[k] | b[k] : a[k] ^ b[k];
  return from_twos(out);
}

BigInt BigInt::bit_and(const BigInt& o) const { return bitwise(o, 0); }
BigInt BigInt::bit_or(const BigInt& o) const { return bitwise(o, 1); }
BigInt BigInt::bit_xor(const BigInt& o) const { return bitwise(o, 2); }

}
}
