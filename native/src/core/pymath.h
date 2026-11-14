#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/accounts.h"
#include "core/util.h"

namespace tcr {
namespace py {

double hypot(double x, double y);

bool pack_f32(Bytes& out, double x, Raised* raised);

std::string decode_utf16le_replace(const uint8_t* data, size_t n);
Bytes encode_utf16le(const std::string& text);

template <class T, class Less>
void list_sort(std::vector<T>& v, Less lt);

}
}

#include "core/pysort.inc"
