#include "core/codec.h"

#include "core/wire.h"

namespace tcr {
namespace codec {

namespace {

bool split(const std::string& token, std::string* kind, int64_t* count) {
  size_t star = token.find('*');
  *kind = token.substr(0, star);
  std::string c = star == std::string::npos ? std::string() : token.substr(star + 1);
  if (c.empty()) {
    *count = 1;
    return true;
  }
  std::optional<std::string> text = py::parse_int(c);
  if (!text) return false;
  std::optional<int64_t> v = py::int_text_to_i64(*text);
  if (!v || *v > (1 << 20) || *v < -(1 << 20)) return false;
  *count = *v;
  return true;
}

int size_of(const std::string& kind) {
  if (kind == "u8") return 1;
  if (kind == "u16") return 2;
  if (kind == "u32") return 4;
  if (kind == "u64") return 8;
  return 0;
}

bool container(const std::string& kind) { return kind == "arr" || kind == "list" || kind == "blob"; }

Bytes empty(const std::string& kind) {
  if (kind == "arr") return wire::empty_array();
  if (kind == "list") return wire::blob(wire::serializer_list({}));
  return wire::blob(Bytes());
}

}

std::optional<Bytes> encode_default(const std::vector<std::string>& layout, const std::map<size_t, Bytes>& overrides) {
  Bytes out;
  for (size_t k = 0; k < layout.size(); k++) {
    auto ov = overrides.find(k);
    if (ov != overrides.end()) {
      out.insert(out.end(), ov->second.begin(), ov->second.end());
      continue;
    }
    std::string kind;
    int64_t count = 0;
    if (!split(layout[k], &kind, &count)) return std::nullopt;
    if (container(kind)) {
      Bytes e = empty(kind);
      for (int64_t i = 0; i < count; i++) out.insert(out.end(), e.begin(), e.end());
    } else if (k == 0 && kind == "u8") {
      out.push_back(1);
      for (int64_t i = 1; i < count; i++) out.push_back(0);
    } else {
      int size = size_of(kind);
      if (!size) return std::nullopt;
      for (int64_t i = 0; i < count * size; i++) out.push_back(0);
    }
  }
  return out;
}

std::optional<std::vector<Item>> decode(const std::vector<std::string>& layout, const Bytes& body) {
  std::vector<Item> values;
  size_t pos = 0;
  for (const std::string& token : layout) {
    std::string kind;
    int64_t count = 0;
    if (!split(token, &kind, &count)) return std::nullopt;
    for (int64_t i = 0; i < count; i++) {
      Item item;
      if (container(kind)) {
        if (pos + 4 > body.size()) return std::nullopt;
        uint64_t n = wire::get_u32(body.data() + pos);
        size_t a = pos + 4 > body.size() ? body.size() : pos + 4;
        size_t b = a + n > body.size() ? body.size() : static_cast<size_t>(a + n);
        item.is_bytes = true;
        item.bytes.assign(body.begin() + static_cast<std::ptrdiff_t>(a), body.begin() + static_cast<std::ptrdiff_t>(b));
        pos += 4 + static_cast<size_t>(n);
      } else {
        int size = size_of(kind);
        if (!size) return std::nullopt;
        if (pos + static_cast<size_t>(size) > body.size()) return std::nullopt;
        uint64_t v = 0;
        for (int k = 0; k < size; k++) v = (v << 8) | body[pos + static_cast<size_t>(k)];
        item.value = v;
        pos += static_cast<size_t>(size);
      }
      values.push_back(std::move(item));
    }
  }
  return values;
}

}
}
