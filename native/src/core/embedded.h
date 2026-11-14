#pragma once

#include <cstddef>
#include <string_view>

namespace tcr {
namespace embedded {

struct Table {
  const char* name;
  const unsigned char* data;
  size_t size;
};

extern const Table kTables[];
extern const size_t kTableCount;

inline std::string_view find(std::string_view name) {
  for (size_t i = 0; i < kTableCount; i++) {
    if (name == kTables[i].name) return std::string_view(reinterpret_cast<const char*>(kTables[i].data), kTables[i].size);
  }
  return std::string_view();
}

}
}
