#include "core/session_store.h"

#include <cmath>

namespace tcr {
namespace session {

namespace {

enum class Conv { kOk, kCaught, kOverflow };

Conv to_float(const json::Value& v, double* out) {
  switch (v.type()) {
    case json::Type::Bool:
      *out = v.as_bool() ? 1.0 : 0.0;
      return Conv::kOk;
    case json::Type::Int:
      *out = static_cast<double>(*v.as_i64());
      return Conv::kOk;
    case json::Type::BigInt: {
      std::optional<double> d = py::int_text_to_double(v.int_text());
      if (!d) return Conv::kOverflow;
      *out = *d;
      return Conv::kOk;
    }
    case json::Type::Float:
      *out = v.as_float();
      return Conv::kOk;
    case json::Type::String: {
      std::optional<double> d = py::parse_float(v.as_string());
      if (!d) return Conv::kCaught;
      *out = *d;
      return Conv::kOk;
    }
    default:
      return Conv::kCaught;
  }
}

}

Loaded load(const std::optional<std::string>& file, double now, Files* files) {
  Loaded out;
  if (!file) return out;
  std::string text;
  if (decode_text_file(files, *file, true, &text)) return out;
  json::Failure why = json::Failure::kSyntax;
  std::optional<json::Value> entries = json::loads(py::universal_newlines(text), json::kFileMaxDepth, &why);
  if (!entries && why != json::Failure::kSyntax) {
    out.error = "RecursionError: " + json::recursion_text(why);
    return out;
  }
  if (!entries || !entries->is_array()) return out;
  for (const json::Value& e : entries->items()) {
    if (!e.is_object()) continue;
    const json::Value* addr = e.get("addr");
    if (!addr || !addr->is_string()) continue;
    const std::string& a = addr->as_string();
    size_t colon = a.rfind(':');
    if (colon == std::string::npos) continue;
    const json::Value* time = e.get("time");
    if (!time) continue;
    double t = 0.0;
    Conv c = to_float(*time, &t);
    if (c == Conv::kOverflow) {
      out.error = "OverflowError: int too large to convert to float";
      return out;
    }
    if (c == Conv::kCaught) continue;
    if (now - t > STORE_MAX_AGE) continue;
    const json::Value* key = e.get("key");
    if (!key || !key->is_string()) continue;
    std::optional<Bytes> k = py::fromhex(key->as_string());
    if (!k) continue;
    const json::Value* ivv = e.get("iv");
    std::optional<Bytes> iv;
    if (!ivv) {
      iv = Bytes();
    } else if (ivv->is_string()) {
      iv = py::fromhex(ivv->as_string());
    }
    if (!iv) continue;
    std::optional<std::string> port = py::parse_int(a.substr(colon + 1));
    if (!port) continue;
    Stored s;
    s.ip = a.substr(0, colon);
    s.port = *port;
    s.key = *k;
    s.iv = *iv;
    if (const json::Value* n = e.get("name")) s.name = *n;
    if (const json::Value* p = e.get("platform")) s.platform = *p;
    s.stamp = t;
    bool replaced = false;
    for (Stored& old : out.sessions) {
      if (old.ip == s.ip && old.port == s.port) {
        old = s;
        replaced = true;
        break;
      }
    }
    if (!replaced) out.sessions.push_back(std::move(s));
  }
  return out;
}

std::string store_text(const std::vector<Stored>& sessions) {
  json::Value entries = json::Value::array();
  for (const Stored& s : sessions) {
    json::Value e = json::Value::object();
    e.set("addr", json::Value::string(s.ip + ":" + s.port));
    e.set("key", json::Value::string(py::hex(s.key)));
    e.set("iv", json::Value::string(py::hex(s.iv)));
    e.set("name", s.name);
    e.set("platform", s.platform);
    e.set("time", json::Value::number(s.stamp));
    entries.push(std::move(e));
  }
  return json::dumps(entries, 1);
}

Loaded SessionStore::load(double now) const {
  std::string data;
  OsError err;
  if (!files_.read(path_, &data, &err)) return Loaded{};
  return session::load(data, now, &files_);
}

std::optional<std::string> SessionStore::save(const std::vector<Stored>& connected) const {
  FileStat st;
  OsError err;
  if (files_.write_atomic(path_, store_text(connected), true, -1, -1, &st, &err)) return std::nullopt;
  return "could not write " + path_ + ": " + err.text();
}

}
}
