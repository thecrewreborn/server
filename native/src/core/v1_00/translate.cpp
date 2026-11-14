#include "core/v1_00/translate.h"

#include <algorithm>
#include <map>
#include <tuple>
#include <unordered_map>

#include "core/codec.h"
#include "core/v1_21/pack.h"
#include "core/wire.h"

namespace tcr {
namespace v1_00 {
namespace translate {

namespace {

size_t size_of(const std::string& kind) {
  if (kind == "u8") return 1;
  if (kind == "u16") return 2;
  if (kind == "u32") return 4;
  if (kind == "u64") return 8;
  return 0;
}

bool container(const std::string& kind) { return kind == "arr" || kind == "list" || kind == "blob"; }

Raised key_error(const std::string& kind) { return Raised{"KeyError", py::repr(kind)}; }

std::string kind_class(const std::string& kind) {
  if (kind == "u8") return "b";
  if (kind == "u16" || kind == "u32") return "n";
  if (kind == "u64") return "q";
  if (kind == "list") return "blob";
  return kind;
}

class Matcher {
 public:
  Matcher(const std::vector<std::string>& a, const std::vector<std::string>& b) : a_(a), b_(b) {
    for (size_t j = 0; j < b_.size(); j++) b2j_[b_[j]].push_back(j);
  }

  std::vector<std::tuple<size_t, size_t, size_t>> matching_blocks() const {
    std::vector<std::tuple<size_t, size_t, size_t>> blocks;
    std::vector<std::tuple<size_t, size_t, size_t, size_t>> queue{{0, a_.size(), 0, b_.size()}};
    while (!queue.empty()) {
      auto [alo, ahi, blo, bhi] = queue.back();
      queue.pop_back();
      size_t i, j, k;
      longest(alo, ahi, blo, bhi, &i, &j, &k);
      if (k) {
        blocks.emplace_back(i, j, k);
        if (alo < i && blo < j) queue.emplace_back(alo, i, blo, j);
        if (i + k < ahi && j + k < bhi) queue.emplace_back(i + k, ahi, j + k, bhi);
      }
    }
    std::sort(blocks.begin(), blocks.end());
    std::vector<std::tuple<size_t, size_t, size_t>> out;
    size_t i1 = 0, j1 = 0, k1 = 0;
    for (auto [i2, j2, k2] : blocks) {
      if (i1 + k1 == i2 && j1 + k1 == j2) {
        k1 += k2;
      } else {
        if (k1) out.emplace_back(i1, j1, k1);
        i1 = i2;
        j1 = j2;
        k1 = k2;
      }
    }
    if (k1) out.emplace_back(i1, j1, k1);
    return out;
  }

 private:
  void longest(size_t alo, size_t ahi, size_t blo, size_t bhi, size_t* bi, size_t* bj, size_t* bsize) const {
    size_t besti = alo, bestj = blo, bestsize = 0;
    std::map<size_t, size_t> j2len;
    for (size_t i = alo; i < ahi; i++) {
      std::map<size_t, size_t> next;
      auto it = b2j_.find(a_[i]);
      if (it != b2j_.end()) {
        for (size_t j : it->second) {
          if (j < blo) continue;
          if (j >= bhi) break;
          auto prev = j > 0 ? j2len.find(j - 1) : j2len.end();
          size_t k = (prev == j2len.end() ? 0 : prev->second) + 1;
          next[j] = k;
          if (k > bestsize) {
            besti = i - k + 1;
            bestj = j - k + 1;
            bestsize = k;
          }
        }
      }
      j2len = std::move(next);
    }
    while (besti > alo && bestj > blo && a_[besti - 1] == b_[bestj - 1]) {
      besti--;
      bestj--;
      bestsize++;
    }
    while (besti + bestsize < ahi && bestj + bestsize < bhi && a_[besti + bestsize] == b_[bestj + bestsize]) {
      bestsize++;
    }
    *bi = besti;
    *bj = bestj;
    *bsize = bestsize;
  }

  const std::vector<std::string>& a_;
  const std::vector<std::string>& b_;
  std::unordered_map<std::string, std::vector<size_t>> b2j_;
};

bool default_value(const std::string& kind, bool first, Bytes* out, Raised* raised) {
  if (container(kind) || first) {
    std::optional<Bytes> b = codec::encode_default({kind});
    if (!b) {
      *raised = key_error(kind);
      return false;
    }
    out->insert(out->end(), b->begin(), b->end());
    return true;
  }
  size_t n = size_of(kind);
  if (!n) {
    *raised = key_error(kind);
    return false;
  }
  out->resize(out->size() + n, 0);
  return true;
}

}

std::vector<std::string> expand(const std::vector<std::string>& layout) {
  std::vector<std::string> out;
  for (const std::string& token : layout) {
    size_t star = token.find('*');
    std::string kind = token.substr(0, star);
    size_t count = 1;
    if (star != std::string::npos && star + 1 < token.size()) {
      count = 0;
      for (size_t i = star + 1; i < token.size(); i++) {
        char c = token[i];
        if (c < '0' || c > '9') break;
        count = count * 10 + static_cast<size_t>(c - '0');
      }
    }
    for (size_t i = 0; i < count; i++) out.push_back(kind);
  }
  return out;
}

Mapping align(const std::vector<std::string>& layout121, const std::vector<std::string>& layout100) {
  std::vector<std::string> a, b;
  for (const std::string& k : expand(layout121)) a.push_back(kind_class(k));
  for (const std::string& k : expand(layout100)) b.push_back(kind_class(k));
  Mapping out(b.size());
  for (auto [i, j, k] : Matcher(a, b).matching_blocks()) {
    for (size_t n = 0; n < k; n++) out[j + n] = i + n;
  }
  return out;
}

bool decode(const std::vector<std::string>& tokens, const Bytes& body, std::vector<Value>* out, Raised* raised) {
  out->clear();
  size_t pos = 0;
  for (const std::string& kind : tokens) {
    if (container(kind)) {
      if (!v1_21::pack::unpack_check(body, pos, 4, raised)) return false;
      size_t n = wire::get_u32(body.data() + pos);
      if (pos + 4 + n > body.size()) {
        *raised = v1_21::pack::error("container runs past the body");
        return false;
      }
      out->push_back(Value::of(Bytes(body.begin() + static_cast<std::ptrdiff_t>(pos),
                                     body.begin() + static_cast<std::ptrdiff_t>(pos + 4 + n))));
      pos += 4 + n;
    } else {
      size_t size = size_of(kind);
      if (!size) {
        *raised = key_error(kind);
        return false;
      }
      if (!v1_21::pack::unpack_check(body, pos, size, raised)) return false;
      uint64_t v = 0;
      for (size_t i = 0; i < size; i++) v = v << 8 | body[pos + i];
      out->push_back(Value::of(v));
      pos += size;
    }
  }
  return true;
}

bool encode(const std::vector<std::string>& tokens, const std::vector<Value>& values, Bytes* out, Raised* raised) {
  out->clear();
  size_t n = std::min(tokens.size(), values.size());
  for (size_t k = 0; k < n; k++) {
    const std::string& kind = tokens[k];
    const Value& value = values[k];
    if (value.kind == Value::kNone) {
      if (!default_value(kind, k == 0, out, raised)) return false;
    } else if (container(kind)) {
      if (value.kind == Value::kBytes) {
        out->insert(out->end(), value.bytes.begin(), value.bytes.end());
      } else if (!default_value(kind, false, out, raised)) {
        return false;
      }
    } else if (value.kind == Value::kBytes) {
      if (!default_value(kind, k == 0, out, raised)) return false;
    } else {
      size_t size = size_of(kind);
      if (!size) {
        *raised = key_error(kind);
        return false;
      }
      for (size_t i = size; i-- > 0;) out->push_back(static_cast<uint8_t>(value.num >> (8 * i)));
    }
  }
  return true;
}

bool transcode(const Bytes& body121, const std::vector<std::string>& layout121,
               const std::vector<std::string>& layout100, const Mapping* mapping, Bytes* out, Raised* raised) {
  std::vector<std::string> t121 = expand(layout121), t100 = expand(layout100);
  std::vector<Value> values121;
  if (!decode(t121, body121, &values121, raised)) return false;
  Mapping aligned;
  if (!mapping) {
    aligned = align(layout121, layout100);
    mapping = &aligned;
  }
  std::vector<Value> values;
  for (const std::optional<size_t>& m : *mapping) {
    if (!m) {
      values.push_back(Value::none());
    } else if (*m >= values121.size()) {
      *raised = Raised{"IndexError", "list index out of range"};
      return false;
    } else {
      values.push_back(values121[*m]);
    }
  }
  return encode(t100, values, out, raised);
}

}
}
}
