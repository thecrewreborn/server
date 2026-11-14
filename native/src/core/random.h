#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tcr {

class Random {
 public:
  static constexpr int kStateWords = 624;

  Random();
  explicit Random(uint64_t seed);

  static Random from_le_bytes(const uint8_t* le, size_t n);
  static Random from_words(const uint32_t* words, size_t n);

  void seed(uint64_t seed);
  void seed_le_bytes(const uint8_t* le, size_t n);
  void seed_words(const uint32_t* words, size_t n);

  uint32_t next_u32();

  uint64_t getrandbits(unsigned k);
  void getrandbits_le(uint64_t k, uint8_t* out);
  std::vector<uint8_t> randbytes(size_t n);
  void randbytes(uint8_t* out, size_t n);

  double random();
  double uniform(double a, double b);

  uint64_t randbelow(uint64_t n);
  std::optional<int64_t> randrange(int64_t start, int64_t stop, int64_t step = 1);
  std::optional<int64_t> randrange(int64_t stop);
  std::optional<int64_t> randint(int64_t a, int64_t b);
  std::optional<size_t> choices_index(const double* weights, size_t n);

 private:
  void init_genrand(uint32_t s);
  void init_by_array(const uint32_t* key, size_t key_length);

  std::array<uint32_t, kStateWords> mt_{};
  int index_ = kStateWords + 1;
};

}
