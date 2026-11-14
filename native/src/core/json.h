#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tcr {
namespace json {

enum class Type : uint8_t { Null, Bool, Int, BigInt, Float, String, Array, Object };

struct Member;

class Value {
 public:
  Value() = default;
  Value(const Value& o);
  Value(Value&& o) noexcept = default;
  Value& operator=(const Value& o);
  Value& operator=(Value&& o) noexcept;
  ~Value();

  static Value null() { return Value(); }
  static Value boolean(bool b);
  static Value integer(int64_t i);
  static Value uinteger(uint64_t u);
  static Value integer_text(std::string decimal);
  static Value number(double f);
  static Value string(std::string text);
  static Value array();
  static Value object();
  static Value object(std::vector<Member> members);

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_int() const { return type_ == Type::Int || type_ == Type::BigInt; }
  bool is_float() const { return type_ == Type::Float; }
  bool is_number() const { return is_int() || is_float(); }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  bool as_bool() const { return b_; }
  double as_float() const { return f_; }
  const std::string& as_string() const { return s_; }
  std::optional<int64_t> as_i64() const;
  std::optional<uint64_t> as_u64() const;
  std::string int_text() const;

  const std::vector<Value>& items() const { return items_; }
  std::vector<Value>& items() { return items_; }
  void push(Value v) { items_.push_back(std::move(v)); }

  const std::vector<Member>& members() const { return members_; }
  std::vector<Member>& members() { return members_; }
  const Value* get(std::string_view key) const;
  Value* get(std::string_view key);
  bool contains(std::string_view key) const { return get(key) != nullptr; }
  void set(std::string key, Value v);
  bool erase(std::string_view key);

  size_t size() const;

  bool operator==(const Value& o) const;
  bool operator!=(const Value& o) const { return !(*this == o); }

 private:
  Type type_ = Type::Null;
  bool b_ = false;
  int64_t i_ = 0;
  double f_ = 0.0;
  std::string s_;
  std::vector<Value> items_;
  std::vector<Member> members_;
};

struct Member {
  std::string key;
  Value value;
  bool int_key = false;
};

constexpr size_t kHttpMaxDepth = 9996;
constexpr size_t kFileMaxDepth = 9998;

enum class Failure { kSyntax, kRecursionArray, kRecursionObject };
std::string recursion_text(Failure f);

struct Error {
  std::string type;
  std::string text;
  void set_decode_error(std::string_view msg, std::string_view doc, size_t pos);
};

std::optional<Value> loads(std::string_view text, size_t max_depth = kFileMaxDepth, Failure* why = nullptr,
                           Error* error = nullptr);
std::optional<Value> loads_bytes(const uint8_t* data, size_t n, size_t max_depth = kHttpMaxDepth,
                                 Failure* why = nullptr, Error* error = nullptr);
std::optional<Value> loads_utf8(const uint8_t* data, size_t n, size_t max_depth = kFileMaxDepth,
                                Failure* why = nullptr, Error* error = nullptr);
inline std::optional<Value> loads_utf8(std::string_view data, size_t max_depth = kFileMaxDepth,
                                       Failure* why = nullptr, Error* error = nullptr) {
  return loads_utf8(reinterpret_cast<const uint8_t*>(data.data()), data.size(), max_depth, why, error);
}

std::string dumps(const Value& v, int indent = -1);

}
}
