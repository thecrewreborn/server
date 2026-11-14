#include "core/json.h"

#include <cmath>
#include <cstdlib>
#include <new>
#include <unordered_map>

#include "core/util.h"

namespace tcr {
namespace json {

Value Value::boolean(bool b) {
  Value v;
  v.type_ = Type::Bool;
  v.b_ = b;
  return v;
}

Value Value::integer(int64_t i) {
  Value v;
  v.type_ = Type::Int;
  v.i_ = i;
  return v;
}

Value Value::uinteger(uint64_t u) {
  if (u <= static_cast<uint64_t>(INT64_MAX)) return integer(static_cast<int64_t>(u));
  Value v;
  v.type_ = Type::BigInt;
  v.s_ = py::uint_to_text(u);
  return v;
}

Value Value::integer_text(std::string decimal) {
  if (std::optional<int64_t> i = py::int_text_to_i64(decimal)) return integer(*i);
  Value v;
  v.type_ = Type::BigInt;
  v.s_ = std::move(decimal);
  return v;
}

Value Value::number(double f) {
  Value v;
  v.type_ = Type::Float;
  v.f_ = f;
  return v;
}

Value Value::string(std::string text) {
  Value v;
  v.type_ = Type::String;
  v.s_ = std::move(text);
  return v;
}

Value Value::array() {
  Value v;
  v.type_ = Type::Array;
  return v;
}

Value Value::object() {
  Value v;
  v.type_ = Type::Object;
  return v;
}

Value Value::object(std::vector<Member> members) {
  Value v;
  v.type_ = Type::Object;
  v.members_ = std::move(members);
  return v;
}

std::optional<int64_t> Value::as_i64() const {
  if (type_ == Type::Int) return i_;
  return std::nullopt;
}

std::optional<uint64_t> Value::as_u64() const {
  if (type_ == Type::Int) return i_ >= 0 ? std::optional<uint64_t>(static_cast<uint64_t>(i_)) : std::nullopt;
  if (type_ == Type::BigInt) return py::int_text_to_u64(s_);
  return std::nullopt;
}

std::string Value::int_text() const {
  if (type_ == Type::Int) return py::int_to_text(i_);
  if (type_ == Type::BigInt) return s_;
  return std::string();
}

const Value* Value::get(std::string_view key) const {
  for (const Member& m : members_)
    if (!m.int_key && m.key == key) return &m.value;
  return nullptr;
}

Value* Value::get(std::string_view key) {
  for (Member& m : members_)
    if (!m.int_key && m.key == key) return &m.value;
  return nullptr;
}

void Value::set(std::string key, Value v) {
  if (Value* old = get(key)) {
    *old = std::move(v);
    return;
  }
  members_.push_back(Member{std::move(key), std::move(v)});
}

bool Value::erase(std::string_view key) {
  for (size_t i = 0; i < members_.size(); i++) {
    if (!members_[i].int_key && members_[i].key == key) {
      members_.erase(members_.begin() + static_cast<std::ptrdiff_t>(i));
      return true;
    }
  }
  return false;
}

size_t Value::size() const {
  switch (type_) {
    case Type::Array:
      return items_.size();
    case Type::Object:
      return members_.size();
    case Type::String:
      return py::length(s_);
    default:
      return 0;
  }
}

bool Value::operator==(const Value& o) const {
  std::vector<std::pair<const Value*, const Value*>> todo{{this, &o}};
  while (!todo.empty()) {
    const Value* x = todo.back().first;
    const Value* y = todo.back().second;
    todo.pop_back();
    if (x->type_ != y->type_) return false;
    switch (x->type_) {
      case Type::Null:
        break;
      case Type::Bool:
        if (x->b_ != y->b_) return false;
        break;
      case Type::Int:
        if (x->i_ != y->i_) return false;
        break;
      case Type::Float:
        if (!(x->f_ == y->f_)) return false;
        break;
      case Type::BigInt:
      case Type::String:
        if (x->s_ != y->s_) return false;
        break;
      case Type::Array:
        if (x->items_.size() != y->items_.size()) return false;
        for (size_t k = 0; k < x->items_.size(); k++) todo.emplace_back(&x->items_[k], &y->items_[k]);
        break;
      case Type::Object:
        if (x->members_.size() != y->members_.size()) return false;
        for (const Member& m : x->members_) {
          const Value* other = nullptr;
          for (const Member& n : y->members_) {
            if (n.int_key == m.int_key && n.key == m.key) other = &n.value;
          }
          if (!other) return false;
          todo.emplace_back(&m.value, other);
        }
        break;
    }
  }
  return true;
}

Value::Value(const Value& o) {
  std::vector<std::pair<const Value*, Value*>> todo{{&o, this}};
  while (!todo.empty()) {
    const Value* src = todo.back().first;
    Value* dst = todo.back().second;
    todo.pop_back();
    dst->type_ = src->type_;
    dst->b_ = src->b_;
    dst->i_ = src->i_;
    dst->f_ = src->f_;
    dst->s_ = src->s_;
    dst->items_.resize(src->items_.size());
    for (size_t k = 0; k < src->items_.size(); k++) todo.emplace_back(&src->items_[k], &dst->items_[k]);
    dst->members_.resize(src->members_.size());
    for (size_t k = 0; k < src->members_.size(); k++) {
      dst->members_[k].key = src->members_[k].key;
      dst->members_[k].int_key = src->members_[k].int_key;
      todo.emplace_back(&src->members_[k].value, &dst->members_[k].value);
    }
  }
}

Value& Value::operator=(const Value& o) {
  if (this != &o) {
    Value copy(o);
    *this = std::move(copy);
  }
  return *this;
}

Value& Value::operator=(Value&& o) noexcept {
  if (this != &o) {
    Value old(std::move(*this));
    type_ = o.type_;
    b_ = o.b_;
    i_ = o.i_;
    f_ = o.f_;
    s_ = std::move(o.s_);
    items_ = std::move(o.items_);
    members_ = std::move(o.members_);
    o.items_.clear();
    o.members_.clear();
  }
  return *this;
}

namespace {

class FreeList {
 public:
  FreeList() = default;
  FreeList(const FreeList&) = delete;
  FreeList& operator=(const FreeList&) = delete;
  ~FreeList() {
    while (n_) pop();
    std::free(data_);
  }
  void push(Value&& v) {
    if (n_ == cap_ && !grow()) {
      alignas(Value) unsigned char kept[sizeof(Value)];
      new (kept) Value(std::move(v));
      return;
    }
    new (data_ + n_) Value(std::move(v));
    n_++;
  }
  Value pop() {
    Value v(std::move(data_[n_ - 1]));
    data_[n_ - 1].~Value();
    n_--;
    return v;
  }
  bool empty() const { return n_ == 0; }

 private:
  bool grow() {
    size_t cap = cap_ ? cap_ * 2 : 64;
    Value* d = static_cast<Value*>(std::malloc(cap * sizeof(Value)));
    if (!d) return false;
    for (size_t k = 0; k < n_; k++) {
      new (d + k) Value(std::move(data_[k]));
      data_[k].~Value();
    }
    std::free(data_);
    data_ = d;
    cap_ = cap;
    return true;
  }
  Value* data_ = nullptr;
  size_t n_ = 0;
  size_t cap_ = 0;
};

}

Value::~Value() {
  if (items_.empty() && members_.empty()) return;
  FreeList todo;
  auto take = [&todo](Value& v) {
    for (Value& c : v.items_) todo.push(std::move(c));
    for (Member& m : v.members_) todo.push(std::move(m.value));
    v.items_.clear();
    v.members_.clear();
  };
  take(*this);
  while (!todo.empty()) {
    Value v = todo.pop();
    take(v);
  }
}

std::string recursion_text(Failure f) {
  return std::string("maximum recursion depth exceeded while decoding a JSON ") +
         (f == Failure::kRecursionObject ? "object" : "array") + " from a unicode string";
}

namespace {

class Parser {
 public:
  Parser(std::string_view s, size_t max_depth) : s_(s), max_depth_(max_depth) {}

  std::optional<Value> run(Failure* why, Error* error) {
    why_ = why;
    error_ = error;
    if (why_) *why_ = Failure::kSyntax;
    size_t i = skip_ws(0);
    Value v;
    size_t end = 0;
    while (true) {
      if (i < s_.size() && (s_[i] == '{' || s_[i] == '[')) {
        bool obj = s_[i] == '{';
        if (stack_.size() + 1 > max_depth_) {
          Failure f = obj ? Failure::kRecursionObject : Failure::kRecursionArray;
          if (why_) *why_ = f;
          if (error_) *error_ = Error{"RecursionError", recursion_text(f)};
          return std::nullopt;
        }
        stack_.emplace_back();
        Frame& f = stack_.back();
        f.object = obj;
        i = skip_ws(i + 1);
        if (i < s_.size() && s_[i] == (obj ? '}' : ']')) {
          v = close_top();
          end = i + 1;
        } else {
          if (obj && !key(i, &i)) return std::nullopt;
          continue;
        }
      } else if (!scalar(i, &v, &end)) {
        return std::nullopt;
      }
      while (true) {
        if (stack_.empty()) {
          end = skip_ws(end);
          if (end != s_.size()) return fail("Extra data", end);
          return v;
        }
        Frame& f = stack_.back();
        if (f.object) {
          auto it = f.index.find(f.key);
          if (it != f.index.end()) {
            f.members[it->second].value = std::move(v);
          } else {
            f.index.emplace(f.key, f.members.size());
            f.members.push_back(Member{std::move(f.key), std::move(v)});
          }
        } else {
          f.items.push_back(std::move(v));
        }
        const char close = f.object ? '}' : ']';
        i = skip_ws(end);
        if (i < s_.size() && s_[i] == close) {
          v = close_top();
          end = i + 1;
          continue;
        }
        if (i >= s_.size() || s_[i] != ',') return fail("Expecting ',' delimiter", i);
        const size_t comma = i;
        i = skip_ws(i + 1);
        if (i < s_.size() && s_[i] == close) {
          return fail(f.object ? "Illegal trailing comma before end of object"
                               : "Illegal trailing comma before end of array",
                      comma);
        }
        if (f.object && !key(i, &i)) return std::nullopt;
        break;
      }
    }
  }

 private:
  size_t skip_ws(size_t i) const {
    while (i < s_.size() && (s_[i] == ' ' || s_[i] == '\t' || s_[i] == '\n' || s_[i] == '\r')) i++;
    return i;
  }

  bool match(size_t i, std::string_view word) const { return s_.substr(i, word.size()) == word; }

  std::nullopt_t fail(const char* msg, size_t pos) {
    if (error_) error_->set_decode_error(msg, s_, pos);
    return std::nullopt;
  }

  struct Frame {
    bool object = false;
    std::vector<Value> items;
    std::vector<Member> members;
    std::unordered_map<std::string, size_t> index;
    std::string key;
  };

  Value close_top() {
    Frame& f = stack_.back();
    Value v = f.object ? Value::object(std::move(f.members)) : Value::array();
    if (!f.object) v.items() = std::move(f.items);
    stack_.pop_back();
    return v;
  }

  bool key(size_t i, size_t* next) {
    if (i >= s_.size() || s_[i] != '"') {
      fail("Expecting property name enclosed in double quotes", i);
      return false;
    }
    Frame& f = stack_.back();
    f.key.clear();
    if (!string(i + 1, &f.key, &i)) return false;
    i = skip_ws(i);
    if (i >= s_.size() || s_[i] != ':') {
      fail("Expecting ':' delimiter", i);
      return false;
    }
    *next = skip_ws(i + 1);
    return true;
  }

  bool scalar(size_t i, Value* out, size_t* end) {
    if (i >= s_.size()) return expecting_value(i);
    char c = s_[i];
    switch (c) {
      case '"': {
        std::string text;
        if (!string(i + 1, &text, end)) return false;
        *out = Value::string(std::move(text));
        return true;
      }
      case 'n':
        if (!match(i, "null")) return expecting_value(i);
        *out = Value::null();
        *end = i + 4;
        return true;
      case 't':
        if (!match(i, "true")) return expecting_value(i);
        *out = Value::boolean(true);
        *end = i + 4;
        return true;
      case 'f':
        if (!match(i, "false")) return expecting_value(i);
        *out = Value::boolean(false);
        *end = i + 5;
        return true;
      case 'N':
        if (!match(i, "NaN")) return expecting_value(i);
        *out = Value::number(std::nan(""));
        *end = i + 3;
        return true;
      case 'I':
        if (!match(i, "Infinity")) return expecting_value(i);
        *out = Value::number(HUGE_VAL);
        *end = i + 8;
        return true;
      case '-':
        if (i + 1 < s_.size() && s_[i + 1] == 'I') {
          if (!match(i, "-Infinity")) return expecting_value(i);
          *out = Value::number(-HUGE_VAL);
          *end = i + 9;
          return true;
        }
        return number(i, out, end);
      default:
        if (c >= '0' && c <= '9') return number(i, out, end);
        return expecting_value(i);
    }
  }

  bool expecting_value(size_t i) {
    fail("Expecting value", i);
    return false;
  }

  static bool digit(char c) { return c >= '0' && c <= '9'; }

  bool number(size_t start, Value* out, size_t* end) {
    size_t i = start, n = s_.size();
    if (s_[i] == '-') {
      i++;
      if (i >= n) return expecting_value(start);
    }
    if (s_[i] >= '1' && s_[i] <= '9') {
      i++;
      while (i < n && digit(s_[i])) i++;
    } else if (s_[i] == '0') {
      i++;
    } else {
      return expecting_value(start);
    }
    bool is_float = false;
    if (i + 1 < n && s_[i] == '.' && digit(s_[i + 1])) {
      is_float = true;
      i += 2;
      while (i < n && digit(s_[i])) i++;
    }
    if (i < n && (s_[i] == 'e' || s_[i] == 'E')) {
      size_t e_start = i;
      i++;
      if (i < n && (s_[i] == '-' || s_[i] == '+')) i++;
      while (i < n && digit(s_[i])) i++;
      if (digit(s_[i - 1])) {
        is_float = true;
      } else {
        i = e_start;
      }
    }
    std::string_view text = s_.substr(start, i - start);
    if (is_float) {
      std::optional<double> f = py::parse_float(text);
      if (!f) return expecting_value(start);
      *out = Value::number(*f);
    } else {
      std::string too_long;
      std::optional<std::string> d = py::parse_int(text, &too_long);
      if (!d) {
        if (error_) *error_ = Error{"ValueError", too_long};
        return false;
      }
      *out = Value::integer_text(std::move(*d));
    }
    *end = i;
    return true;
  }

  bool hex4(size_t i, uint32_t* out) const {
    uint32_t v = 0;
    for (size_t k = 0; k < 4; k++) {
      int h = py::hex_value(static_cast<uint8_t>(s_[i + k]));
      if (h < 0) return false;
      v = (v << 4) | static_cast<uint32_t>(h);
    }
    *out = v;
    return true;
  }

  bool string(size_t i, std::string* out, size_t* end) {
    const size_t n = s_.size();
    const size_t begin = i - 1;
    while (true) {
      size_t chunk = i;
      while (i < n && s_[i] != '"' && s_[i] != '\\') {
        if (static_cast<uint8_t>(s_[i]) < 0x20) {
          fail("Invalid control character at", i);
          return false;
        }
        i++;
      }
      out->append(s_.data() + chunk, i - chunk);
      if (i >= n) {
        fail("Unterminated string starting at", begin);
        return false;
      }
      if (s_[i] == '"') {
        *end = i + 1;
        return true;
      }
      i++;
      if (i >= n) {
        fail("Unterminated string starting at", begin);
        return false;
      }
      char c = s_[i];
      if (c != 'u') {
        switch (c) {
          case '"':
            *out += '"';
            break;
          case '\\':
            *out += '\\';
            break;
          case '/':
            *out += '/';
            break;
          case 'b':
            *out += '\b';
            break;
          case 'f':
            *out += '\f';
            break;
          case 'n':
            *out += '\n';
            break;
          case 'r':
            *out += '\r';
            break;
          case 't':
            *out += '\t';
            break;
          default:
            fail("Invalid \\escape", i - 1);
            return false;
        }
        i++;
        continue;
      }
      uint32_t cp = 0;
      if (i + 5 >= n || !hex4(i + 1, &cp)) {
        fail("Invalid \\uXXXX escape", i);
        return false;
      }
      i += 5;
      if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < n && s_[i] == '\\' && s_[i + 1] == 'u') {
        uint32_t c2 = 0;
        if (!hex4(i + 2, &c2)) {
          fail("Invalid \\uXXXX escape", i + 1);
          return false;
        }
        if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
          cp = 0x10000 + (((cp - 0xD800) << 10) | (c2 - 0xDC00));
          i += 6;
        }
      }
      py::append_utf8(*out, cp);
    }
  }

  std::string_view s_;
  size_t max_depth_;
  std::vector<Frame> stack_;
  Failure* why_ = nullptr;
  Error* error_ = nullptr;
};

}

void Error::set_decode_error(std::string_view msg, std::string_view doc, size_t pos) {
  size_t chars = 0, line = 1, last_newline_chars = 0;
  bool newline_seen = false;
  for (size_t k = 0; k < pos && k < doc.size(); k++) {
    uint8_t b = static_cast<uint8_t>(doc[k]);
    if ((b & 0xC0) == 0x80) continue;
    if (b == '\n') {
      line++;
      last_newline_chars = chars;
      newline_seen = true;
    }
    chars++;
  }
  size_t column = newline_seen ? chars - last_newline_chars : chars + 1;
  type = "json.decoder.JSONDecodeError";
  text = std::string(msg) + ": line " + std::to_string(line) + " column " + std::to_string(column) + " (char " +
         std::to_string(chars) + ")";
}

namespace {

std::optional<Value> parse(std::string_view text, size_t max_depth, Failure* why, Error* error) {
  return Parser(text, max_depth).run(why, error);
}

}

std::optional<Value> loads(std::string_view text, size_t max_depth, Failure* why, Error* error) {
  if (why) *why = Failure::kSyntax;
  if (text.substr(0, 3) == "\xEF\xBB\xBF") {
    if (error) error->set_decode_error("Unexpected UTF-8 BOM (decode using utf-8-sig)", text, 0);
    return std::nullopt;
  }
  return parse(text, max_depth, why, error);
}

namespace {

bool decode_utf16(const uint8_t* p, size_t n, bool big, std::string* out) {
  if (n % 2) return false;
  size_t units = n / 2;
  auto unit = [&](size_t k) -> uint32_t {
    return big ? (static_cast<uint32_t>(p[2 * k]) << 8) | p[2 * k + 1]
               : (static_cast<uint32_t>(p[2 * k + 1]) << 8) | p[2 * k];
  };
  for (size_t k = 0; k < units; k++) {
    uint32_t u = unit(k);
    if (u >= 0xD800 && u <= 0xDBFF && k + 1 < units) {
      uint32_t u2 = unit(k + 1);
      if (u2 >= 0xDC00 && u2 <= 0xDFFF) {
        py::append_utf8(*out, 0x10000 + (((u - 0xD800) << 10) | (u2 - 0xDC00)));
        k++;
        continue;
      }
    }
    py::append_utf8(*out, u);
  }
  return true;
}

bool decode_utf32(const uint8_t* p, size_t n, bool big, std::string* out) {
  if (n % 4) return false;
  for (size_t k = 0; k < n; k += 4) {
    uint32_t u = big ? (static_cast<uint32_t>(p[k]) << 24) | (static_cast<uint32_t>(p[k + 1]) << 16) |
                           (static_cast<uint32_t>(p[k + 2]) << 8) | p[k + 3]
                     : (static_cast<uint32_t>(p[k + 3]) << 24) | (static_cast<uint32_t>(p[k + 2]) << 16) |
                           (static_cast<uint32_t>(p[k + 1]) << 8) | p[k];
    if (u > 0x10FFFF) return false;
    py::append_utf8(*out, u);
  }
  return true;
}

}

std::optional<Value> loads_bytes(const uint8_t* b, size_t n, size_t max_depth, Failure* why, Error* error) {
  if (why) *why = Failure::kSyntax;
  std::string text;
  bool ok;
  auto starts = [&](std::initializer_list<uint8_t> prefix) {
    if (n < prefix.size()) return false;
    size_t k = 0;
    for (uint8_t c : prefix)
      if (b[k++] != c) return false;
    return true;
  };
  if (starts({0x00, 0x00, 0xFE, 0xFF})) {
    ok = decode_utf32(b + 4, n - 4, true, &text);
  } else if (starts({0xFF, 0xFE, 0x00, 0x00})) {
    ok = decode_utf32(b + 4, n - 4, false, &text);
  } else if (starts({0xFE, 0xFF})) {
    ok = decode_utf16(b + 2, n - 2, true, &text);
  } else if (starts({0xFF, 0xFE})) {
    ok = decode_utf16(b + 2, n - 2, false, &text);
  } else if (starts({0xEF, 0xBB, 0xBF})) {
    ok = py::decode_utf8(b + 3, n - 3, true, &text, nullptr);
  } else if (n >= 4 && !b[0]) {
    ok = b[1] ? decode_utf16(b, n, true, &text) : decode_utf32(b, n, true, &text);
  } else if (n >= 4 && !b[1]) {
    ok = (b[2] || b[3]) ? decode_utf16(b, n, false, &text) : decode_utf32(b, n, false, &text);
  } else if (n == 2 && !b[0]) {
    ok = decode_utf16(b, n, true, &text);
  } else if (n == 2 && !b[1]) {
    ok = decode_utf16(b, n, false, &text);
  } else {
    ok = py::decode_utf8(b, n, true, &text, nullptr);
  }
  if (!ok) {
    if (error) *error = Error{"UnicodeDecodeError", ""};
    return std::nullopt;
  }
  return parse(text, max_depth, why, error);
}

std::optional<Value> loads_utf8(const uint8_t* data, size_t n, size_t max_depth, Failure* why, Error* error) {
  if (why) *why = Failure::kSyntax;
  std::string text;
  if (!py::decode_utf8(data, n, false, &text, nullptr)) {
    if (error) *error = Error{"UnicodeDecodeError", ""};
    return std::nullopt;
  }
  return loads(text, max_depth, why, error);
}

namespace {

void write_string(std::string& out, std::string_view s) {
  static const char kHex[] = "0123456789abcdef";
  auto u4 = [&](uint32_t u) {
    out += "\\u";
    for (int k = 3; k >= 0; k--) out += kHex[(u >> (4 * k)) & 0xF];
  };
  out += '"';
  for (uint32_t c : py::code_points(s)) {
    if (c >= ' ' && c <= '~' && c != '\\' && c != '"') {
      out += static_cast<char>(c);
      continue;
    }
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c >= 0x10000) {
          uint32_t v = c - 0x10000;
          u4(0xD800 | (v >> 10));
          u4(0xDC00 | (v & 0x3FF));
        } else {
          u4(c);
        }
    }
  }
  out += '"';
}

void write_float(std::string& out, double f) {
  if (std::isnan(f)) {
    out += "NaN";
  } else if (std::isinf(f)) {
    out += f < 0 ? "-Infinity" : "Infinity";
  } else {
    out += py::float_repr(f);
  }
}

void newline(std::string& out, int indent, int level) {
  out += '\n';
  out.append(static_cast<size_t>(indent) * static_cast<size_t>(level), ' ');
}

struct Open {
  const Value* v;
  size_t next;
};

void write_one(std::string& out, const Value& v, std::vector<Open>& stack) {
  switch (v.type()) {
    case Type::Null:
      out += "null";
      return;
    case Type::Bool:
      out += v.as_bool() ? "true" : "false";
      return;
    case Type::Int:
    case Type::BigInt:
      out += v.int_text();
      return;
    case Type::Float:
      write_float(out, v.as_float());
      return;
    case Type::String:
      write_string(out, v.as_string());
      return;
    case Type::Array:
      if (v.items().empty()) {
        out += "[]";
        return;
      }
      out += '[';
      stack.push_back(Open{&v, 0});
      return;
    case Type::Object:
      if (v.members().empty()) {
        out += "{}";
        return;
      }
      out += '{';
      stack.push_back(Open{&v, 0});
      return;
  }
}

void write(std::string& out, const Value& root, int indent) {
  std::vector<Open> stack;
  write_one(out, root, stack);
  while (!stack.empty()) {
    Open& top = stack.back();
    const Value& v = *top.v;
    const bool obj = v.is_object();
    const size_t n = obj ? v.members().size() : v.items().size();
    const int level = static_cast<int>(stack.size());
    if (top.next == n) {
      if (indent >= 0) newline(out, indent, level - 1);
      out += obj ? '}' : ']';
      stack.pop_back();
      continue;
    }
    size_t k = top.next++;
    if (indent >= 0) {
      if (k) out += ',';
      newline(out, indent, level);
    } else if (k) {
      out += ", ";
    }
    if (obj) {
      write_string(out, v.members()[k].key);
      out += ": ";
      write_one(out, v.members()[k].value, stack);
    } else {
      write_one(out, v.items()[k], stack);
    }
  }
}

}

std::string dumps(const Value& v, int indent) {
  std::string out;
  write(out, v, indent);
  return out;
}

}
}
