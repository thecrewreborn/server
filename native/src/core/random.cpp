#include "core/random.h"

#include <cmath>
#include <cstring>

namespace tcr {

namespace {

constexpr int N = Random::kStateWords;
constexpr int M = 397;
constexpr uint32_t kMatrixA = 0x9908b0dfU;
constexpr uint32_t kUpperMask = 0x80000000U;
constexpr uint32_t kLowerMask = 0x7fffffffU;

unsigned bit_length(uint64_t n) {
  unsigned k = 0;
  while (n) {
    ++k;
    n >>= 1;
  }
  return k;
}

}

Random::Random() { seed(0); }

Random::Random(uint64_t s) { seed(s); }

Random Random::from_le_bytes(const uint8_t* le, size_t n) {
  Random r;
  r.seed_le_bytes(le, n);
  return r;
}

Random Random::from_words(const uint32_t* words, size_t n) {
  Random r;
  r.seed_words(words, n);
  return r;
}

void Random::init_genrand(uint32_t s) {
  mt_[0] = s;
  for (int i = 1; i < N; i++) {
    mt_[i] = 1812433253U * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + static_cast<uint32_t>(i);
  }
  index_ = N;
}

void Random::init_by_array(const uint32_t* key, size_t key_length) {
  init_genrand(19650218U);
  size_t i = 1;
  size_t j = 0;
  for (size_t k = (static_cast<size_t>(N) > key_length ? static_cast<size_t>(N) : key_length); k; k--) {
    mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1664525U)) + key[j] + static_cast<uint32_t>(j);
    i++;
    j++;
    if (i >= static_cast<size_t>(N)) {
      mt_[0] = mt_[N - 1];
      i = 1;
    }
    if (j >= key_length) j = 0;
  }
  for (size_t k = N - 1; k; k--) {
    mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1566083941U)) - static_cast<uint32_t>(i);
    i++;
    if (i >= static_cast<size_t>(N)) {
      mt_[0] = mt_[N - 1];
      i = 1;
    }
  }
  mt_[0] = 0x80000000U;
}

void Random::seed(uint64_t s) {
  uint32_t key[2] = {static_cast<uint32_t>(s), static_cast<uint32_t>(s >> 32)};
  init_by_array(key, key[1] ? 2 : 1);
}

void Random::seed_le_bytes(const uint8_t* le, size_t n) {
  while (n > 0 && le[n - 1] == 0) n--;
  if (n == 0) {
    uint32_t zero = 0;
    init_by_array(&zero, 1);
    return;
  }
  std::vector<uint32_t> key((n + 3) / 4, 0);
  for (size_t i = 0; i < n; i++) key[i / 4] |= static_cast<uint32_t>(le[i]) << (8 * (i % 4));
  init_by_array(key.data(), key.size());
}

void Random::seed_words(const uint32_t* words, size_t n) {
  if (n == 0) {
    uint32_t zero = 0;
    init_by_array(&zero, 1);
    return;
  }
  init_by_array(words, n);
}

uint32_t Random::next_u32() {
  static const uint32_t mag01[2] = {0x0U, kMatrixA};
  uint32_t y;
  if (index_ >= N) {
    int kk;
    for (kk = 0; kk < N - M; kk++) {
      y = (mt_[kk] & kUpperMask) | (mt_[kk + 1] & kLowerMask);
      mt_[kk] = mt_[kk + M] ^ (y >> 1) ^ mag01[y & 0x1U];
    }
    for (; kk < N - 1; kk++) {
      y = (mt_[kk] & kUpperMask) | (mt_[kk + 1] & kLowerMask);
      mt_[kk] = mt_[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1U];
    }
    y = (mt_[N - 1] & kUpperMask) | (mt_[0] & kLowerMask);
    mt_[N - 1] = mt_[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
    index_ = 0;
  }
  y = mt_[index_++];
  y ^= (y >> 11);
  y ^= (y << 7) & 0x9d2c5680U;
  y ^= (y << 15) & 0xefc60000U;
  y ^= (y >> 18);
  return y;
}

uint64_t Random::getrandbits(unsigned k) {
  if (k == 0) return 0;
  if (k > 64) k = 64;
  if (k <= 32) return next_u32() >> (32 - k);
  uint64_t low = next_u32();
  uint64_t high = next_u32() >> (64 - k);
  return low | (high << 32);
}

void Random::getrandbits_le(uint64_t k, uint8_t* out) {
  size_t bytes = static_cast<size_t>((k + 7) / 8);
  size_t pos = 0;
  int64_t left = static_cast<int64_t>(k);
  while (left > 0) {
    uint32_t r = next_u32();
    if (left < 32) r >>= (32 - left);
    for (int b = 0; b < 4 && pos < bytes; b++) out[pos++] = static_cast<uint8_t>(r >> (8 * b));
    left -= 32;
  }
}

std::vector<uint8_t> Random::randbytes(size_t n) {
  std::vector<uint8_t> out(n);
  if (n) randbytes(out.data(), n);
  return out;
}

void Random::randbytes(uint8_t* out, size_t n) {
  if (n == 0) return;
  getrandbits_le(static_cast<uint64_t>(n) * 8, out);
}

double Random::random() {
  uint32_t a = next_u32() >> 5;
  uint32_t b = next_u32() >> 6;
  return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}

double Random::uniform(double a, double b) { return a + (b - a) * random(); }

uint64_t Random::randbelow(uint64_t n) {
  if (n == 0) return 0;
  unsigned k = bit_length(n);
  uint64_t r = getrandbits(k);
  while (r >= n) r = getrandbits(k);
  return r;
}

std::optional<int64_t> Random::randrange(int64_t stop) {
  if (stop > 0) return static_cast<int64_t>(randbelow(static_cast<uint64_t>(stop)));
  return std::nullopt;
}

std::optional<int64_t> Random::randrange(int64_t start, int64_t stop, int64_t step) {
  const uint64_t ustart = static_cast<uint64_t>(start);
  if (step == 1) {
    if (stop <= start) return std::nullopt;
    uint64_t width = static_cast<uint64_t>(stop) - ustart;
    return static_cast<int64_t>(ustart + randbelow(width));
  }
  if (step == 0) return std::nullopt;
  uint64_t width;
  uint64_t ustep;
  if (step > 0) {
    if (stop <= start) return std::nullopt;
    width = static_cast<uint64_t>(stop) - ustart;
    ustep = static_cast<uint64_t>(step);
  } else {
    if (stop >= start) return std::nullopt;
    width = ustart - static_cast<uint64_t>(stop);
    ustep = uint64_t(0) - static_cast<uint64_t>(step);
  }
  uint64_t n = width / ustep + (width % ustep != 0);
  return static_cast<int64_t>(ustart + static_cast<uint64_t>(step) * randbelow(n));
}

std::optional<int64_t> Random::randint(int64_t a, int64_t b) {
  if (b == INT64_MAX) return std::nullopt;
  return randrange(a, b + 1, 1);
}

std::optional<size_t> Random::choices_index(const double* weights, size_t n) {
  if (n == 0) return std::nullopt;
  std::vector<double> cum(n);
  double sum = 0.0;
  for (size_t i = 0; i < n; i++) {
    sum = i ? sum + weights[i] : weights[i];
    cum[i] = sum;
  }
  double total = cum[n - 1] + 0.0;
  if (!(total > 0.0)) return std::nullopt;
  if (!std::isfinite(total)) return std::nullopt;
  double x = random() * total;
  size_t lo = 0;
  size_t hi = n - 1;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (x < cum[mid]) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return lo;
}

}
