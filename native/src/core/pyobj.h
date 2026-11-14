#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/accounts.h"
#include "core/json.h"
#include "core/util.h"

namespace tcr {
namespace obj {

using V = json::Value;

inline V I(int64_t i) { return V::integer(i); }
inline V U(uint64_t u) { return V::uinteger(u); }
inline V F(double f) { return V::number(f); }
inline V B(bool b) { return V::boolean(b); }
V list_of(const std::vector<int64_t>& xs);
V list_of_u64(const std::vector<uint64_t>& xs);

const char* type_name(const V& v);
bool truthy(const V& v);
bool eq(const V& a, const V& b);
bool same(const V& a, const V& b);
inline bool hashable(const V& v) { return !v.is_array() && !v.is_object(); }
std::optional<int64_t> small_int(const V& v);
std::string repr(const V& v);
std::string str(const V& v);
std::string dumps(const V& v);

class Py {
 public:
  bool failed() const { return err_; }
  const Raised& raised() const { return raised_; }
  bool raise(const char* type, std::string text);
  bool raise(const Raised& r) { return raise(r.type.c_str(), r.text); }

  bool lt(const V& a, const V& b) { return compare(a, b, 0); }
  bool le(const V& a, const V& b) { return compare(a, b, 1); }
  bool gt(const V& a, const V& b) { return compare(a, b, 2); }
  bool ge(const V& a, const V& b) { return compare(a, b, 3); }

  V add(const V& a, const V& b, bool inplace = false);
  V sub(const V& a, const V& b, bool inplace = false);
  V mul(const V& a, const V& b, bool inplace = false);
  V floordiv(const V& a, const V& b, bool inplace = false);
  V mod(const V& a, const V& b, bool inplace = false);
  V lshift(const V& a, const V& b, bool inplace = false);
  V rshift(const V& a, const V& b, bool inplace = false);
  V band(const V& a, const V& b, bool inplace = false);
  V bor(const V& a, const V& b, bool inplace = false);
  V neg(const V& a);

  V to_int(const V& a);
  V to_float(const V& a);
  std::optional<int64_t> index(const V& a, const char* what);

  V min(const V& a, const V& b);
  V max(const V& a, const V& b);
  V sum(const V& iterable);
  size_t len(const V& a);
  std::vector<V> iter(const V& a);
  bool unpack(const V& a, size_t n, std::vector<V>* out);
  bool contains(const V& container, const V& item);
  V getitem(const V& c, const V& key);
  bool setitem(V& c, const V& key, V value);
  V* item_ref(V& c, const V& key);
  bool is_dict(const V& v) const { return v.is_object(); }

  bool pack(Bytes& out, char code, const V& v);
  std::string format_hex(const V& v);
  std::optional<Bytes> fromhex(const V& v);

 private:
  bool err_ = false;
  Raised raised_;
  bool compare(const V& a, const V& b, int op);
  V arith(const V& a, const V& b, int op, bool inplace);
};

void float_divmod(double a, double b, double* q, double* r);

}
}
