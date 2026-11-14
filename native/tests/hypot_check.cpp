#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/pymath.h"

namespace {

struct Case {
  uint64_t x, y, r;
};

const Case kCases[] = {
#include "hypot_vectors.inc"
};

double from_bits(uint64_t b) {
  double d;
  std::memcpy(&d, &b, sizeof(d));
  return d;
}

uint64_t to_bits(double d) {
  uint64_t b;
  std::memcpy(&b, &d, sizeof(b));
  return b;
}

}

int main() {
  size_t differ = 0;
  const size_t n = sizeof(kCases) / sizeof(kCases[0]);
  for (const Case& c : kCases) {
    uint64_t got = to_bits(tcr::py::hypot(from_bits(c.x), from_bits(c.y)));
    bool nan = got != c.r && from_bits(got) != from_bits(got) && from_bits(c.r) != from_bits(c.r);
    if (got != c.r && !nan) {
      if (differ < 20) {
        std::printf("differs: hypot(%016llx, %016llx) = %016llx, Python %016llx\n",
                    static_cast<unsigned long long>(c.x), static_cast<unsigned long long>(c.y),
                    static_cast<unsigned long long>(got), static_cast<unsigned long long>(c.r));
      }
      ++differ;
    }
  }
  if (differ) {
    std::printf("hypot: %zu cases, %zu differ\n", n, differ);
    return 1;
  }
  std::printf("hypot: %zu cases, all as Python\n", n);
  return 0;
}
