#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tcr {
namespace py {

class BigInt {
 public:
  BigInt() = default;
  static BigInt of(int64_t v);
  static BigInt of_unsigned(uint64_t v);
  static std::optional<BigInt> parse(std::string_view decimal);

  std::string text() const;
  bool negative() const { return neg_; }
  bool zero() const { return mag_.empty(); }
  std::optional<int64_t> to_i64() const;
  size_t bit_length() const;

  int compare(const BigInt& o) const;
  BigInt operator-() const;
  BigInt operator+(const BigInt& o) const;
  BigInt operator-(const BigInt& o) const;
  BigInt operator*(const BigInt& o) const;
  static void divmod(const BigInt& a, const BigInt& b, BigInt* q, BigInt* r);
  BigInt shift_left(uint64_t n) const;
  BigInt shift_right(uint64_t n) const;
  BigInt bit_and(const BigInt& o) const;
  BigInt bit_or(const BigInt& o) const;
  BigInt bit_xor(const BigInt& o) const;

 private:
  bool neg_ = false;
  std::vector<uint32_t> mag_;

  void trim();
  static int compare_mag(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b);
  static std::vector<uint32_t> add_mag(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b);
  static std::vector<uint32_t> sub_mag(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b);
  std::vector<uint32_t> twos(size_t n) const;
  static BigInt from_twos(const std::vector<uint32_t>& t);
  BigInt bitwise(const BigInt& o, int op) const;
};

}
}
