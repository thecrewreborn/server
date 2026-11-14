#include "core/pyobj.h"

#include <cmath>
#include <utility>

#include "core/bigint.h"

namespace tcr {
namespace obj {

namespace {

using py::BigInt;

bool integral(const V& v) { return v.is_bool() || v.is_int(); }
bool numeric(const V& v) { return integral(v) || v.is_float(); }

BigInt big_of(const V& v) {
  if (v.is_bool()) return BigInt::of(v.as_bool() ? 1 : 0);
  if (v.type() == json::Type::Int) return BigInt::of(*v.as_i64());
  return BigInt::parse(v.int_text()).value_or(BigInt());
}

V from_big(const BigInt& b) {
  if (std::optional<int64_t> i = b.to_i64()) return I(*i);
  return V::integer_text(b.text());
}

std::string int_text_of(const V& v) { return v.is_bool() ? (v.as_bool() ? "1" : "0") : v.int_text(); }

bool double_of(const V& v, double* out) {
  if (v.is_float()) {
    *out = v.as_float();
    return true;
  }
  if (std::optional<int64_t> i = small_int(v)) {
    *out = static_cast<double>(*i);
    return true;
  }
  std::optional<double> d = py::int_text_to_double(v.int_text());
  if (!d) return false;
  *out = *d;
  return true;
}

std::optional<int> compare_numbers(const V& a, const V& b) {
  if (a.is_float() && b.is_float()) {
    double x = a.as_float(), y = b.as_float();
    if (std::isnan(x) || std::isnan(y)) return std::nullopt;
    return x < y ? -1 : x > y ? 1 : 0;
  }
  if (a.is_float()) {
    std::optional<int> c = py::compare_int_float(int_text_of(b), a.as_float());
    if (!c) return std::nullopt;
    return -*c;
  }
  if (b.is_float()) return py::compare_int_float(int_text_of(a), b.as_float());
  std::optional<int64_t> x = small_int(a), y = small_int(b);
  if (x && y) return *x < *y ? -1 : *x > *y ? 1 : 0;
  return big_of(a).compare(big_of(b));
}

const char* const kCompareSymbols[] = {"<", "<=", ">", ">="};
const char* const kArithSymbols[] = {"+", "-", "*", "//", "%", "<<", ">>", "&", "|"};
enum { ADD, SUB, MUL, FLOORDIV, MOD, LSHIFT, RSHIFT, AND, OR };

const json::Member* member_of(const V& d, const V& key) {
  for (const json::Member& m : d.members()) {
    if (key.is_string()) {
      if (!m.int_key && m.key == key.as_string()) return &m;
    } else if (m.int_key) {
      std::optional<BigInt> k = BigInt::parse(m.key);
      if (k && eq(from_big(*k), key)) return &m;
    }
  }
  return nullptr;
}

bool same_or_eq(const V& a, const V& b) { return same(a, b); }

}

V list_of(const std::vector<int64_t>& xs) {
  V out = V::array();
  for (int64_t x : xs) out.push(I(x));
  return out;
}

V list_of_u64(const std::vector<uint64_t>& xs) {
  V out = V::array();
  for (uint64_t x : xs) out.push(U(x));
  return out;
}

const char* type_name(const V& v) {
  switch (v.type()) {
    case json::Type::Null:
      return "NoneType";
    case json::Type::Bool:
      return "bool";
    case json::Type::Int:
    case json::Type::BigInt:
      return "int";
    case json::Type::Float:
      return "float";
    case json::Type::String:
      return "str";
    case json::Type::Array:
      return "list";
    case json::Type::Object:
      return "dict";
  }
  return "object";
}

bool truthy(const V& v) {
  switch (v.type()) {
    case json::Type::Null:
      return false;
    case json::Type::Bool:
      return v.as_bool();
    case json::Type::Int:
      return *v.as_i64() != 0;
    case json::Type::BigInt:
      return true;
    case json::Type::Float:
      return v.as_float() != 0.0;
    case json::Type::String:
      return !v.as_string().empty();
    case json::Type::Array:
      return !v.items().empty();
    case json::Type::Object:
      return !v.members().empty();
  }
  return false;
}

std::optional<int64_t> small_int(const V& v) {
  if (v.is_bool()) return v.as_bool() ? 1 : 0;
  if (v.type() == json::Type::Int) return *v.as_i64();
  return std::nullopt;
}

bool same(const V& a, const V& b) {
  if (a.is_float() && b.is_float() && std::isnan(a.as_float()) && std::isnan(b.as_float())) return true;
  return eq(a, b);
}

bool eq(const V& a, const V& b) {
  if (numeric(a) && numeric(b)) {
    std::optional<int> c = compare_numbers(a, b);
    return c && *c == 0;
  }
  if (a.type() != b.type()) return false;
  switch (a.type()) {
    case json::Type::Null:
      return true;
    case json::Type::String:
      return a.as_string() == b.as_string();
    case json::Type::Array: {
      if (a.items().size() != b.items().size()) return false;
      for (size_t i = 0; i < a.items().size(); i++) {
        if (!same_or_eq(a.items()[i], b.items()[i])) return false;
      }
      return true;
    }
    case json::Type::Object: {
      if (a.members().size() != b.members().size()) return false;
      for (const json::Member& m : a.members()) {
        const json::Member* o = nullptr;
        for (const json::Member& n : b.members()) {
          if (n.int_key == m.int_key && n.key == m.key) o = &n;
        }
        if (!o || !same_or_eq(m.value, o->value)) return false;
      }
      return true;
    }
    default:
      return false;
  }
}

std::string repr(const V& v) {
  switch (v.type()) {
    case json::Type::Null:
      return "None";
    case json::Type::Bool:
      return v.as_bool() ? "True" : "False";
    case json::Type::Int:
    case json::Type::BigInt:
      return v.int_text();
    case json::Type::Float:
      return py::float_repr(v.as_float());
    case json::Type::String:
      return py::repr(v.as_string());
    case json::Type::Array: {
      std::string out = "[";
      for (size_t i = 0; i < v.items().size(); i++) out += (i ? ", " : "") + repr(v.items()[i]);
      return out + "]";
    }
    case json::Type::Object: {
      std::string out = "{";
      for (size_t i = 0; i < v.members().size(); i++) {
        const json::Member& m = v.members()[i];
        out += (i ? ", " : "") + (m.int_key ? m.key : py::repr(m.key)) + ": " + repr(m.value);
      }
      return out + "}";
    }
  }
  return "";
}

std::string str(const V& v) { return v.is_string() ? v.as_string() : repr(v); }

std::string dumps(const V& v) { return json::dumps(v); }

void float_divmod(double vx, double wx, double* q, double* r) {
  double mod = std::fmod(vx, wx);
  double div = (vx - mod) / wx;
  if (mod) {
    if ((wx < 0) != (mod < 0)) {
      mod += wx;
      div -= 1.0;
    }
  } else {
    mod = std::copysign(0.0, wx);
  }
  double floordiv;
  if (div) {
    floordiv = std::floor(div);
    if (div - floordiv > 0.5) floordiv += 1.0;
  } else {
    floordiv = std::copysign(0.0, vx / wx);
  }
  *q = floordiv;
  *r = mod;
}

bool Py::raise(const char* type, std::string text) {
  if (!err_) {
    err_ = true;
    raised_ = Raised{type, std::move(text)};
  }
  return false;
}

bool Py::compare(const V& a, const V& b, int op) {
  if (err_) return false;
  if (numeric(a) && numeric(b)) {
    std::optional<int> c = compare_numbers(a, b);
    if (!c) return false;
    switch (op) {
      case 0:
        return *c < 0;
      case 1:
        return *c <= 0;
      case 2:
        return *c > 0;
      default:
        return *c >= 0;
    }
  }
  if (a.is_string() && b.is_string()) {
    int c = a.as_string().compare(b.as_string());
    switch (op) {
      case 0:
        return c < 0;
      case 1:
        return c <= 0;
      case 2:
        return c > 0;
      default:
        return c >= 0;
    }
  }
  if (a.is_array() && b.is_array()) {
    size_t n = std::min(a.items().size(), b.items().size());
    for (size_t i = 0; i < n; i++) {
      if (!same_or_eq(a.items()[i], b.items()[i])) return compare(a.items()[i], b.items()[i], op);
    }
    return compare(I(static_cast<int64_t>(a.items().size())), I(static_cast<int64_t>(b.items().size())), op);
  }
  raise("TypeError", std::string("'") + kCompareSymbols[op] + "' not supported between instances of '" +
                         type_name(a) + "' and '" + type_name(b) + "'");
  return false;
}

V Py::arith(const V& a, const V& b, int op, bool inplace) {
  if (err_) return V();
  const std::string sym = std::string(kArithSymbols[op]) + (inplace ? "=" : "");
  auto unsupported = [&]() {
    raise("TypeError", "unsupported operand type(s) for " + sym + ": '" + type_name(a) + "' and '" + type_name(b) +
                           "'");
    return V();
  };
  if (numeric(a) && numeric(b)) {
    if (integral(a) && integral(b)) {
      if (op == AND || op == OR) {
        if (a.is_bool() && b.is_bool()) return B(op == AND ? a.as_bool() && b.as_bool() : a.as_bool() || b.as_bool());
        std::optional<int64_t> x = small_int(a), y = small_int(b);
        if (x && y) return I(op == AND ? (*x & *y) : (*x | *y));
        return from_big(op == AND ? big_of(a).bit_and(big_of(b)) : big_of(a).bit_or(big_of(b)));
      }
      if (op == LSHIFT || op == RSHIFT) {
        BigInt count = big_of(b);
        if (count.negative()) {
          raise("ValueError", "negative shift count");
          return V();
        }
        BigInt x = big_of(a);
        if (op == RSHIFT) {
          std::optional<int64_t> n = count.to_i64();
          if (!n || static_cast<uint64_t>(*n) > x.bit_length()) return I(x.negative() ? -1 : 0);
          return from_big(x.shift_right(static_cast<uint64_t>(*n)));
        }
        if (x.zero()) return I(0);
        std::optional<int64_t> n = count.to_i64();
        if (!n || *n > (int64_t(1) << 32)) {
          raise("OverflowError", "too many digits in integer");
          return V();
        }
        return from_big(x.shift_left(static_cast<uint64_t>(*n)));
      }
      if (op == FLOORDIV || op == MOD) {
        BigInt y = big_of(b);
        if (y.zero()) {
          raise("ZeroDivisionError", op == FLOORDIV ? "integer division or modulo by zero" : "integer modulo by zero");
          return V();
        }
        BigInt q, r;
        BigInt::divmod(big_of(a), y, &q, &r);
        return from_big(op == FLOORDIV ? q : r);
      }
      std::optional<int64_t> x = small_int(a), y = small_int(b);
      const int64_t lim = int64_t(1) << 31;
      if (x && y && *x > -lim && *x < lim && *y > -lim && *y < lim) {
        return I(op == ADD ? *x + *y : op == SUB ? *x - *y : *x * *y);
      }
      BigInt p = big_of(a), q = big_of(b);
      return from_big(op == ADD ? p + q : op == SUB ? p - q : p * q);
    }
    if (op == LSHIFT || op == RSHIFT || op == AND || op == OR) return unsupported();
    double x = 0, y = 0;
    if (!double_of(a, &x) || !double_of(b, &y)) {
      raise("OverflowError", "int too large to convert to float");
      return V();
    }
    switch (op) {
      case ADD:
        return F(x + y);
      case SUB:
        return F(x - y);
      case MUL:
        return F(x * y);
      default: {
        if (y == 0.0) {
          raise("ZeroDivisionError", op == FLOORDIV ? "float floor division by zero" : "float modulo by zero");
          return V();
        }
        double q = 0, r = 0;
        float_divmod(x, y, &q, &r);
        return F(op == FLOORDIV ? q : r);
      }
    }
  }
  if (op == ADD) {
    if (a.is_string()) {
      if (b.is_string()) return V::string(a.as_string() + b.as_string());
      raise("TypeError", std::string("can only concatenate str (not \"") + type_name(b) + "\") to str");
      return V();
    }
    if (a.is_array()) {
      if (b.is_array()) {
        V out = a;
        for (const V& x : b.items()) out.push(x);
        return out;
      }
      if (inplace) {
        std::vector<V> xs = iter(b);
        if (err_) return V();
        V out = a;
        for (V& x : xs) out.push(std::move(x));
        return out;
      }
      raise("TypeError", std::string("can only concatenate list (not \"") + type_name(b) + "\") to list");
      return V();
    }
    return unsupported();
  }
  if (op == MUL && (a.is_string() || a.is_array() || b.is_string() || b.is_array())) {
    const V& seq = a.is_string() || a.is_array() ? a : b;
    const V& count = &seq == &a ? b : a;
    if (!integral(count)) {
      raise("TypeError", std::string("can't multiply sequence by non-int of type '") + type_name(count) + "'");
      return V();
    }
    std::optional<int64_t> n = small_int(count);
    if (!n) {
      raise("OverflowError", "cannot fit 'int' into an index-sized integer");
      return V();
    }
    size_t unit = seq.is_string() ? seq.as_string().size() : seq.items().size();
    if (*n > 0 && unit && static_cast<uint64_t>(*n) > (uint64_t(1) << 28) / unit) {
      raise("MemoryError", "");
      return V();
    }
    if (seq.is_string()) {
      std::string out;
      for (int64_t i = 0; i < *n; i++) out += seq.as_string();
      return V::string(std::move(out));
    }
    V out = V::array();
    for (int64_t i = 0; i < *n; i++) {
      for (const V& x : seq.items()) out.push(x);
    }
    return out;
  }
  if (op == MOD && a.is_string()) {
    if (b.is_array() || b.is_object()) return a;
    raise("TypeError", "not all arguments converted during string formatting");
    return V();
  }
  if (op == OR && a.is_object()) {
    if (b.is_object()) {
      V out = a;
      for (const json::Member& m : b.members()) {
        V key = m.int_key ? V::integer_text(m.key) : V::string(m.key);
        setitem(out, key, m.value);
      }
      return out;
    }
    if (!inplace) return unsupported();
    std::vector<V> xs = iter(b);
    if (err_) return V();
    V out = a;
    for (size_t k = 0; k < xs.size(); k++) {
      const V& e = xs[k];
      if (!e.is_array() && !e.is_object() && !e.is_string()) {
        raise("TypeError", "cannot convert dictionary update sequence element #" + std::to_string(k) +
                               " to a sequence");
        return V();
      }
      std::vector<V> kv = iter(e);
      if (kv.size() != 2) {
        raise("ValueError", "dictionary update sequence element #" + std::to_string(k) + " has length " +
                                std::to_string(kv.size()) + "; 2 is required");
        return V();
      }
      setitem(out, kv[0], kv[1]);
      if (err_) return V();
    }
    return out;
  }
  return unsupported();
}

V Py::add(const V& a, const V& b, bool inplace) { return arith(a, b, ADD, inplace); }
V Py::sub(const V& a, const V& b, bool inplace) { return arith(a, b, SUB, inplace); }
V Py::mul(const V& a, const V& b, bool inplace) { return arith(a, b, MUL, inplace); }
V Py::floordiv(const V& a, const V& b, bool inplace) { return arith(a, b, FLOORDIV, inplace); }
V Py::mod(const V& a, const V& b, bool inplace) { return arith(a, b, MOD, inplace); }
V Py::lshift(const V& a, const V& b, bool inplace) { return arith(a, b, LSHIFT, inplace); }
V Py::rshift(const V& a, const V& b, bool inplace) { return arith(a, b, RSHIFT, inplace); }
V Py::band(const V& a, const V& b, bool inplace) { return arith(a, b, AND, inplace); }
V Py::bor(const V& a, const V& b, bool inplace) { return arith(a, b, OR, inplace); }

V Py::neg(const V& a) {
  if (err_) return V();
  if (a.is_float()) return F(-a.as_float());
  if (integral(a)) {
    if (std::optional<int64_t> x = small_int(a)) {
      if (*x != INT64_MIN) return I(-*x);
    }
    return from_big(-big_of(a));
  }
  raise("TypeError", std::string("bad operand type for unary -: '") + type_name(a) + "'");
  return V();
}

V Py::to_int(const V& a) {
  if (err_) return V();
  if (a.is_bool()) return I(a.as_bool() ? 1 : 0);
  if (a.is_int()) return a;
  if (a.is_float()) {
    std::string type, text;
    std::optional<std::string> t = py::float_to_int_text(a.as_float(), &type, &text);
    if (!t) {
      raise(type.c_str(), text);
      return V();
    }
    return V::integer_text(*t);
  }
  if (a.is_string()) {
    std::string err;
    std::optional<std::string> t = py::parse_int(a.as_string(), &err);
    if (!t) {
      raise("ValueError", err);
      return V();
    }
    return V::integer_text(*t);
  }
  raise("TypeError", std::string("int() argument must be a string, a bytes-like object or a real number, not '") +
                         type_name(a) + "'");
  return V();
}

V Py::to_float(const V& a) {
  if (err_) return V();
  if (numeric(a)) {
    double d = 0;
    if (!double_of(a, &d)) {
      raise("OverflowError", "int too large to convert to float");
      return V();
    }
    return F(d);
  }
  if (a.is_string()) {
    std::string err;
    std::optional<double> d = py::parse_float(a.as_string(), &err);
    if (!d) {
      raise("ValueError", err);
      return V();
    }
    return F(*d);
  }
  raise("TypeError", std::string("float() argument must be a string or a real number, not '") + type_name(a) + "'");
  return V();
}

std::optional<int64_t> Py::index(const V& a, const char* what) {
  if (err_) return std::nullopt;
  if (integral(a)) {
    if (std::optional<int64_t> i = small_int(a)) return i;
    raise("IndexError", "cannot fit 'int' into an index-sized integer");
    return std::nullopt;
  }
  raise("TypeError", std::string(what) + type_name(a));
  return std::nullopt;
}

V Py::min(const V& a, const V& b) {
  if (err_) return V();
  bool less = lt(b, a);
  if (err_) return V();
  return less ? b : a;
}

V Py::max(const V& a, const V& b) {
  if (err_) return V();
  bool more = gt(b, a);
  if (err_) return V();
  return more ? b : a;
}

V Py::sum(const V& iterable) {
  std::vector<V> xs = iter(iterable);
  if (err_) return V();
  size_t k = 0, n = xs.size();
  int64_t total = 0;
  V result;
  bool left_ints = false;
  for (; k < n; k++) {
    const V& x = xs[k];
    std::optional<int64_t> b = small_int(x);
    if (b && ((total >= 0 && *b <= INT64_MAX - total) || (total < 0 && *b >= INT64_MIN - total))) {
      total += *b;
      continue;
    }
    result = add(I(total), x);
    if (err_) return V();
    k++;
    left_ints = true;
    break;
  }
  if (!left_ints) return I(total);
  if (result.is_float()) {
    double f = result.as_float(), c = 0.0;
    bool generic = false;
    for (; k < n; k++) {
      const V& x = xs[k];
      if (x.is_float()) {
        double y = x.as_float(), t = f + y;
        if (std::fabs(f) >= std::fabs(y)) {
          c += (f - t) + y;
        } else {
          c += (y - t) + f;
        }
        f = t;
        continue;
      }
      if (std::optional<int64_t> i = small_int(x)) {
        f += static_cast<double>(*i);
        continue;
      }
      if (c != 0.0 && std::isfinite(c)) f += c;
      result = add(F(f), x);
      if (err_) return V();
      k++;
      generic = true;
      break;
    }
    if (!generic) {
      if (c != 0.0 && std::isfinite(c)) f += c;
      return F(f);
    }
  }
  for (; k < n; k++) {
    result = add(result, xs[k]);
    if (err_) return V();
  }
  return result;
}

size_t Py::len(const V& a) {
  if (err_) return 0;
  if (a.is_array() || a.is_object() || a.is_string()) return a.size();
  raise("TypeError", std::string("object of type '") + type_name(a) + "' has no len()");
  return 0;
}

std::vector<V> Py::iter(const V& a) {
  std::vector<V> out;
  if (err_) return out;
  if (a.is_array()) return a.items();
  if (a.is_object()) {
    for (const json::Member& m : a.members()) {
      out.push_back(m.int_key ? V::integer_text(m.key) : V::string(m.key));
    }
    return out;
  }
  if (a.is_string()) {
    for (uint32_t c : py::code_points(a.as_string())) out.push_back(V::string(py::from_code_points({c})));
    return out;
  }
  raise("TypeError", std::string("'") + type_name(a) + "' object is not iterable");
  return out;
}

bool Py::unpack(const V& a, size_t n, std::vector<V>* out) {
  if (err_) return false;
  if (!a.is_array() && !a.is_object() && !a.is_string()) {
    return raise("TypeError", std::string("cannot unpack non-iterable ") + type_name(a) + " object");
  }
  std::vector<V> xs = iter(a);
  if (xs.size() > n) return raise("ValueError", "too many values to unpack (expected " + std::to_string(n) + ")");
  if (xs.size() < n) {
    return raise("ValueError", "not enough values to unpack (expected " + std::to_string(n) + ", got " +
                                   std::to_string(xs.size()) + ")");
  }
  *out = std::move(xs);
  return true;
}

bool Py::contains(const V& container, const V& item) {
  if (err_) return false;
  if (container.is_array()) {
    for (const V& x : container.items()) {
      if (same_or_eq(x, item)) return true;
    }
    return false;
  }
  if (container.is_object()) {
    if (!hashable(item)) return raise("TypeError", std::string("unhashable type: '") + type_name(item) + "'");
    return member_of(container, item) != nullptr;
  }
  if (container.is_string()) {
    if (!item.is_string()) {
      return raise("TypeError",
                   std::string("'in <string>' requires string as left operand, not ") + type_name(item));
    }
    return container.as_string().find(item.as_string()) != std::string::npos;
  }
  return raise("TypeError", std::string("argument of type '") + type_name(container) + "' is not iterable");
}

V Py::getitem(const V& c, const V& key) {
  if (err_) return V();
  if (c.is_array() || c.is_string()) {
    const bool list = c.is_array();
    if (!integral(key)) {
      raise("TypeError", list ? std::string("list indices must be integers or slices, not ") + type_name(key)
                              : std::string("string indices must be integers, not '") + type_name(key) + "'");
      return V();
    }
    std::optional<int64_t> i = small_int(key);
    if (!i) {
      raise("IndexError", "cannot fit 'int' into an index-sized integer");
      return V();
    }
    std::vector<uint32_t> cps;
    if (!list) cps = py::code_points(c.as_string());
    int64_t n = static_cast<int64_t>(list ? c.items().size() : cps.size());
    int64_t k = *i < 0 ? *i + n : *i;
    if (k < 0 || k >= n) {
      raise("IndexError", list ? "list index out of range" : "string index out of range");
      return V();
    }
    if (list) return c.items()[static_cast<size_t>(k)];
    return V::string(py::from_code_points({cps[static_cast<size_t>(k)]}));
  }
  if (c.is_object()) {
    if (!hashable(key)) {
      raise("TypeError", std::string("unhashable type: '") + type_name(key) + "'");
      return V();
    }
    if (const json::Member* m = member_of(c, key)) return m->value;
    raise("KeyError", repr(key));
    return V();
  }
  raise("TypeError", std::string("'") + type_name(c) + "' object is not subscriptable");
  return V();
}

V* Py::item_ref(V& c, const V& key) {
  if (err_) return nullptr;
  if (c.is_array()) {
    if (!integral(key)) {
      raise("TypeError", std::string("list indices must be integers or slices, not ") + type_name(key));
      return nullptr;
    }
    std::optional<int64_t> i = small_int(key);
    if (!i) {
      raise("IndexError", "cannot fit 'int' into an index-sized integer");
      return nullptr;
    }
    int64_t n = static_cast<int64_t>(c.items().size());
    int64_t k = *i < 0 ? *i + n : *i;
    if (k < 0 || k >= n) {
      raise("IndexError", "list index out of range");
      return nullptr;
    }
    return &c.items()[static_cast<size_t>(k)];
  }
  if (c.is_object()) {
    if (!hashable(key)) {
      raise("TypeError", std::string("unhashable type: '") + type_name(key) + "'");
      return nullptr;
    }
    for (json::Member& m : c.members()) {
      if (&m == member_of(c, key)) return &m.value;
    }
    raise("KeyError", repr(key));
    return nullptr;
  }
  getitem(c, key);
  return nullptr;
}

bool Py::setitem(V& c, const V& key, V value) {
  if (err_) return false;
  if (c.is_array()) {
    if (!integral(key)) {
      return raise("TypeError", std::string("list indices must be integers or slices, not ") + type_name(key));
    }
    std::optional<int64_t> i = small_int(key);
    if (!i) return raise("IndexError", "cannot fit 'int' into an index-sized integer");
    int64_t n = static_cast<int64_t>(c.items().size());
    int64_t k = *i < 0 ? *i + n : *i;
    if (k < 0 || k >= n) return raise("IndexError", "list assignment index out of range");
    c.items()[static_cast<size_t>(k)] = std::move(value);
    return true;
  }
  if (c.is_object()) {
    if (!hashable(key)) return raise("TypeError", std::string("unhashable type: '") + type_name(key) + "'");
    for (json::Member& m : c.members()) {
      if (&m == member_of(c, key)) {
        m.value = std::move(value);
        return true;
      }
    }
    if (key.is_string()) {
      c.set(key.as_string(), std::move(value));
    } else {
      json::Member m;
      m.key = key.is_float() ? py::float_repr(key.as_float()) : int_text_of(key);
      m.value = std::move(value);
      m.int_key = true;
      c.members().push_back(std::move(m));
    }
    return true;
  }
  if (c.is_string() && integral(key) && !small_int(key)) {
    return raise("IndexError", "cannot fit 'int' into an index-sized integer");
  }
  return raise("TypeError", std::string("'") + type_name(c) + "' object does not support item assignment");
}

bool Py::pack(Bytes& out, char code, const V& v) {
  if (err_) return false;
  if (!integral(v)) return raise("struct.error", "required argument is not an integer");
  BigInt x = big_of(v);
  BigInt lo, hi;
  const char* range = nullptr;
  switch (code) {
    case 'B':
      lo = BigInt::of(0), hi = BigInt::of(0xFF), range = "'B' format requires 0 <= number <= 255";
      break;
    case 'H':
      lo = BigInt::of(0), hi = BigInt::of(0xFFFF), range = "'H' format requires 0 <= number <= 65535";
      break;
    case 'I':
      lo = BigInt::of(0), hi = BigInt::of(0xFFFFFFFFll), range = "'I' format requires 0 <= number <= 4294967295";
      break;
    case 'Q':
      lo = BigInt::of(0), hi = BigInt::of_unsigned(~uint64_t(0));
      range = "'Q' format requires 0 <= number <= 18446744073709551615";
      break;
    default:
      lo = BigInt::of(INT64_MIN), hi = BigInt::of(INT64_MAX);
      range = "'q' format requires -9223372036854775808 <= number <= 9223372036854775807";
      break;
  }
  if (x.compare(lo) < 0 || x.compare(hi) > 0) return raise("struct.error", range);
  size_t bytes = code == 'B' ? 1 : code == 'H' ? 2 : code == 'I' ? 4 : 8;
  BigInt m = x.negative() ? x + BigInt::of(1).shift_left(64) : x;
  std::optional<int64_t> lo64 = m.bit_and(BigInt::of_unsigned(0xFFFFFFFFull)).to_i64();
  std::optional<int64_t> hi64 = m.shift_right(32).bit_and(BigInt::of_unsigned(0xFFFFFFFFull)).to_i64();
  uint64_t u = (static_cast<uint64_t>(hi64.value_or(0)) << 32) | static_cast<uint64_t>(lo64.value_or(0));
  for (size_t i = bytes; i-- > 0;) out.push_back(static_cast<uint8_t>(u >> (8 * i)));
  return true;
}

std::optional<Bytes> Py::fromhex(const V& v) {
  if (err_) return std::nullopt;
  if (!v.is_string()) {
    raise("TypeError", std::string("fromhex() argument must be str, not ") + (v.is_null() ? "None" : type_name(v)));
    return std::nullopt;
  }
  std::string err;
  std::optional<Bytes> b = py::fromhex(v.as_string(), &err);
  if (!b) raise("ValueError", err);
  return b;
}

std::string Py::format_hex(const V& v) {
  if (err_) return std::string();
  if (integral(v)) {
    BigInt x = big_of(v);
    bool neg = x.negative();
    if (neg) x = -x;
    static const char kHex[] = "0123456789abcdef";
    std::string digits;
    while (!x.zero()) {
      digits.push_back(kHex[x.bit_and(BigInt::of(15)).to_i64().value_or(0)]);
      x = x.shift_right(4);
    }
    if (digits.empty()) digits = "0";
    std::string out = neg ? "-0x" : "0x";
    for (size_t i = digits.size(); i-- > 0;) out.push_back(digits[i]);
    return out;
  }
  if (v.is_float() || v.is_string()) {
    raise("ValueError", std::string("Unknown format code 'x' for object of type '") + type_name(v) + "'");
  } else {
    raise("TypeError", std::string("unsupported format string passed to ") + type_name(v) + ".__format__");
  }
  return std::string();
}

}
}
