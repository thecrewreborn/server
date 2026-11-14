#include "debug_calls.h"

#include <algorithm>
#include <map>

#include "core/accounts.h"
#include "core/avatar.h"
#include "core/pyobj.h"
#include "core/blowfish.h"
#include "core/codec.h"
#include "core/config.h"
#include "core/core.h"
#include "core/crypto_aes.h"
#include "core/json.h"
#include "core/logtext.h"
#include "core/scrypt.h"
#include "core/ubi.h"
#include "core/session_store.h"
#include "core/util.h"
#include "core/core.h"
#include "core/random.h"
#include "core/v1_00/handlers.h"
#include "core/v1_00/translate.h"
#include "core/v1_21/handlers.h"
#include "core/v1_21/physparts.h"
#include "core/v1_21/player.h"
#include "core/v1_21/relay.h"
#include "platform/files.h"
#include "platform/platform.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace tcr {
namespace debug {

namespace {

using json::Value;

struct Args {
  const Value& v;
  bool ok = true;

  const Value* get(const char* k) {
    const Value* x = v.get(k);
    if (!x) ok = false;
    return x;
  }
  std::string str(const char* k) {
    if (!v.get(k)) {
      const Value* h = v.get(std::string(k) + "_hex");
      std::optional<Bytes> b = h && h->is_string() ? py::fromhex(h->as_string()) : std::nullopt;
      if (b) return std::string(b->begin(), b->end());
    }
    const Value* x = get(k);
    if (!x || !x->is_string()) {
      ok = false;
      return std::string();
    }
    return x->as_string();
  }
  Bytes hex(const char* k) {
    std::optional<Bytes> b = py::fromhex(str(k));
    if (!b) {
      ok = false;
      return Bytes();
    }
    return *b;
  }
  std::optional<Bytes> hex_or_null(const char* k) {
    const Value* x = get(k);
    if (!x || x->is_null()) return std::nullopt;
    return hex(k);
  }
  int64_t i64(const char* k) {
    const Value* x = get(k);
    if (!x || !x->as_i64()) {
      ok = false;
      return 0;
    }
    return *x->as_i64();
  }
  uint64_t u64(const Value& x) {
    std::optional<uint64_t> u = x.as_u64();
    if (!u) ok = false;
    return u ? *u : 0;
  }
  double f64(const char* k) {
    const Value* x = get(k);
    if (!x) return 0.0;
    if (x->is_float()) return x->as_float();
    if (x->as_i64()) return static_cast<double>(*x->as_i64());
    ok = false;
    return 0.0;
  }
  bool flag(const char* k) {
    const Value* x = get(k);
    if (!x || !x->is_bool()) {
      ok = false;
      return false;
    }
    return x->as_bool();
  }
};

Value S(const std::string& s) { return Value::string(s); }
Value H(const Bytes& b) { return Value::string(py::hex(b)); }
Value I(int64_t i) { return Value::integer(i); }
Value B(bool b) { return Value::boolean(b); }
Value opt_s(const std::optional<std::string>& s) { return s ? S(*s) : Value(); }

Value message_value(wire::Parsed p, const wire::Message& m) {
  Value r = Value::object();
  r.set("status", S(p == wire::Parsed::kNone ? "none" : p == wire::Parsed::kOk ? "ok" : "error"));
  if (p != wire::Parsed::kOk) return r;
  r.set("type", Value::uinteger(m.type));
  r.set("length", Value::uinteger(m.length));
  r.set("crc_ok", B(m.crc_ok));
  auto u = [&](const char* k, const std::optional<uint32_t>& x) { r.set(k, x ? Value::uinteger(*x) : Value()); };
  u("transfer", m.transfer);
  u("channel", m.channel);
  r.set("flags", m.flags ? I(*m.flags) : Value());
  u("chunk", m.chunk);
  u("total", m.total);
  r.set("payload", m.payload ? H(*m.payload) : Value());
  u("kind", m.kind);
  r.set("stamp", m.stamp ? H(*m.stamp) : Value());
  u("received", m.received);
  return r;
}

template <class T>
Value result_value(const config::Result<T>& r, Value (*conv)(const T&)) {
  Value out = Value::object();
  out.set("ok", B(r.ok()));
  if (r.ok()) {
    out.set("value", conv(*r.value));
  } else {
    out.set("type", S(r.error.type));
    out.set("message", S(r.error.message));
  }
  return out;
}

Value conv_str(const std::string& s) { return S(s); }
Value conv_int(const std::string& s) { return Value::integer_text(s); }
Value conv_bool(const bool& b) { return B(b); }
Value conv_opt(const std::optional<std::string>& s) { return opt_s(s); }
Value conv_pair(const std::pair<std::string, std::string>& p) {
  Value v = Value::array();
  v.push(S(p.first));
  v.push(S(p.second));
  return v;
}
Value conv_pair_int(const std::pair<std::string, std::string>& p) {
  Value v = Value::array();
  v.push(S(p.first));
  v.push(Value::integer_text(p.second));
  return v;
}
Value conv_key(const std::pair<Bytes, Bytes>& p) {
  Value v = Value::array();
  v.push(H(p.first));
  v.push(H(p.second));
  return v;
}

Value outcome_value(const config::Outcome& o, const std::vector<std::string>& lines) {
  Value v = Value::object();
  v.set("kind", S(o.kind == config::Outcome::kOk ? "ok" : o.kind == config::Outcome::kExit ? "exit" : "crash"));
  v.set("text", S(o.text));
  Value l = Value::array();
  for (const std::string& s : lines) l.push(S(s));
  v.set("lines", l);
  return v;
}

bool trusted_v4(const std::string& ip) {
  std::optional<std::array<uint8_t, 4>> a = wire::inet_aton(ip);
  if (!a) return false;
  uint8_t x = (*a)[0], y = (*a)[1];
  return x == 10 || x == 127 || (x == 172 && y >= 16 && y <= 31) || (x == 192 && y == 168);
}

Value stored_value(const session::Stored& s) {
  Value v = Value::object();
  v.set("ip", S(s.ip));
  v.set("port", Value::integer_text(s.port));
  v.set("key", H(s.key));
  v.set("iv", H(s.iv));
  v.set("name", s.name);
  v.set("platform", s.platform);
  v.set("time", Value::number(s.stamp));
  return v;
}

bool stored_from(Args& a, const Value& list, std::vector<session::Stored>* out) {
  for (const Value& e : list.items()) {
    Args x{e};
    session::Stored s;
    s.ip = x.str("ip");
    const Value* port = x.get("port");
    if (port) s.port = port->int_text();
    s.key = x.hex("key");
    s.iv = x.hex("iv");
    if (const Value* n = x.get("name")) s.name = *n;
    if (const Value* p = x.get("platform")) s.platform = *p;
    s.stamp = x.f64("time");
    if (!x.ok) a.ok = false;
    out->push_back(std::move(s));
  }
  return a.ok;
}

std::vector<std::string> layout_of(Args& a) {
  std::vector<std::string> layout;
  const Value* l = a.get("layout");
  if (!l || !l->is_array()) {
    a.ok = false;
    return layout;
  }
  for (const Value& t : l->items()) layout.push_back(t.is_string() ? t.as_string() : std::string());
  return layout;
}

struct ThreadJob {
  Bytes data;
  std::string mode;
  size_t max_depth = 0;
  int indent = -1;
  bool ok = false;
  bool copy_equal = false;
  std::string dump;
  json::Failure why = json::Failure::kSyntax;

  void run() {
    std::optional<Value> v = mode == "bytes" ? json::loads_bytes(data.data(), data.size(), max_depth, &why)
                                             : json::loads_utf8(data.data(), data.size(), max_depth, &why);
    ok = v.has_value();
    if (!ok) return;
    dump = json::dumps(*v, indent);
    Value copy(*v);
    copy_equal = copy == *v;
    Value moved = std::move(copy);
    copy = Value::array();
    copy = moved;
  }
};

#if defined(_WIN32)
DWORD WINAPI thread_main(LPVOID arg) {
  static_cast<ThreadJob*>(arg)->run();
  return 0;
}
bool run_on_thread(ThreadJob* job, size_t stack) {
  HANDLE h = CreateThread(nullptr, stack, thread_main, job, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (!h) return false;
  WaitForSingleObject(h, INFINITE);
  CloseHandle(h);
  return true;
}
#else
void* thread_main(void* arg) {
  static_cast<ThreadJob*>(arg)->run();
  return nullptr;
}
bool run_on_thread(ThreadJob* job, size_t stack) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  if (pthread_attr_setstacksize(&attr, stack) != 0) {
    pthread_attr_destroy(&attr);
    return false;
  }
  pthread_t t;
  int rc = pthread_create(&t, &attr, thread_main, job);
  pthread_attr_destroy(&attr);
  if (rc != 0) return false;
  pthread_join(t, nullptr);
  return true;
}
#endif

Value earned_value(const std::vector<v1_21::Earned>& book) {
  Value list = Value::array();
  for (const v1_21::Earned& e : book) {
    Value t = Value::array();
    t.push(I(e.time));
    t.push(e.bucks);
    t.push(I(e.xp));
    list.push(std::move(t));
  }
  return list;
}

}

bool call(State& state, const std::string& name, const std::string& args_json, std::string* result) {
  std::optional<Value> parsed = json::loads_utf8(args_json);
  if (!parsed || !parsed->is_object()) {
    *result = "arguments are not a JSON object";
    return false;
  }
  Args a{*parsed};
  Value r = Value::object();

  if (name == "util.repr") {
    r.set("r", S(py::repr(a.str("s"))));
  } else if (name == "util.lower") {
    r.set("r_hex", S(py::hex(py::lower(a.str("s")))));
  } else if (name == "util.strip") {
    r.set("r_hex", S(py::hex(py::strip(a.str("s")))));
    r.set("rstrip_hex", S(py::hex(py::rstrip(a.str("s")))));
  } else if (name == "util.parse_int") {
    std::string error;
    std::optional<std::string> v = py::parse_int(a.str("s"), &error);
    r.set("ok", B(v.has_value()));
    r.set("v", v ? Value::integer_text(*v) : Value());
    r.set("error", S(v ? std::string() : error));
  } else if (name == "util.parse_float") {
    std::string error;
    std::optional<double> v = py::parse_float(a.str("s"), &error);
    r.set("ok", B(v.has_value()));
    r.set("v", v ? Value::number(*v) : Value());
    r.set("error", S(v ? std::string() : error));
  } else if (name == "util.float_repr") {
    Value rs = Value::array();
    if (const Value* fs = a.get("fs")) {
      for (const Value& f : fs->items()) {
        if (!f.is_float()) a.ok = false;
        rs.push(S(py::float_repr(f.as_float())));
      }
    }
    r.set("rs", rs);
  } else if (name == "json.loads_on_thread") {
    ThreadJob job;
    job.data = a.hex("hex");
    job.mode = a.str("mode");
    job.max_depth = static_cast<size_t>(a.i64("max_depth"));
    job.indent = static_cast<int>(a.i64("indent"));
    if (a.ok && !run_on_thread(&job, static_cast<size_t>(a.i64("stack_kb")) * 1024)) {
      *result = "json.loads_on_thread: no thread with that stack";
      return false;
    }
    r.set("ok", B(job.ok));
    r.set("dump", job.ok ? S(job.dump) : Value());
    r.set("copy_equal", B(job.copy_equal));
    r.set("recursion", job.ok || job.why == json::Failure::kSyntax ? Value() : S(json::recursion_text(job.why)));
  } else if (name == "json.limits") {
    r.set("http", I(static_cast<int64_t>(json::kHttpMaxDepth)));
    r.set("file", I(static_cast<int64_t>(json::kFileMaxDepth)));
  } else if (name == "util.int_to_float") {
    const Value* v = a.get("i");
    std::optional<double> d = v ? py::int_text_to_double(v->int_text()) : std::nullopt;
    r.set("v", d ? Value::number(*d) : Value());
  } else if (name == "util.fromhex") {
    std::string error;
    std::optional<Bytes> v = py::fromhex(a.str("s"), &error);
    r.set("ok", B(v.has_value()));
    r.set("hex", v ? H(*v) : Value());
    r.set("error", S(v ? std::string() : error));
  } else if (name == "util.decode_utf8") {
    Bytes b = a.hex("hex");
    std::string text;
    py::DecodeError err;
    bool ok = py::decode_utf8(b.data(), b.size(), a.flag("surrogatepass"), &text, &err);
    r.set("ok", B(ok));
    r.set("text_hex", ok ? S(py::hex(text)) : Value());
    r.set("error", S(ok ? std::string() : py::decode_error_text("utf-8", b.data(), b.size(), err)));
  } else if (name == "util.text_file_error") {
    Bytes b = a.hex("hex");
    int code_page = a.v.get("code_page") ? static_cast<int>(a.i64("code_page")) : 0;
    bool whole = a.v.get("whole") && a.flag("whole");
    std::string decoded;
    r.set("error", opt_s(py::text_file_decode_error(b.data(), b.size(), &decoded, code_page, whole)));
    r.set("decoded_hex", S(py::hex(decoded)));
    r.set("universal_hex", S(py::hex(py::universal_newlines(decoded))));
  } else if (name == "util.posix_join") {
    py::PosixPaths p;
    r.set("join", S(p.join(a.str("a"), a.str("b"))));
    r.set("isabs", B(p.isabs(a.str("a"))));

  } else if (name == "json.loads") {
    Bytes b = a.hex("hex");
    std::string mode = a.str("mode");
    size_t depth = static_cast<size_t>(a.i64("max_depth"));
    int indent = static_cast<int>(a.i64("indent"));
    std::optional<Value> v;
    json::Error error;
    if (mode == "bytes") {
      v = json::loads_bytes(b.data(), b.size(), depth, nullptr, &error);
    } else if (mode == "utf8") {
      v = json::loads_utf8(b.data(), b.size(), depth, nullptr, &error);
    } else {
      std::string text;
      py::DecodeError err;
      if (py::decode_utf8(b.data(), b.size(), true, &text, &err)) {
        v = json::loads(text, depth, nullptr, &error);
      } else {
        error = json::Error{"UnicodeDecodeError", ""};
      }
    }
    r.set("ok", B(v.has_value()));
    r.set("dump", v ? S(json::dumps(*v, indent)) : Value());
    r.set("error", v ? S("") : S(error.type + (error.text.empty() ? "" : ": " + error.text)));

  } else if (name == "wire.crc32") {
    r.set("crc", Value::uinteger(wire::crc32(a.hex("data"))));
  } else if (name == "wire.frame") {
    r.set("hex", H(wire::frame(static_cast<uint32_t>(a.i64("type")), a.hex("body"))));
  } else if (name == "wire.blob") {
    r.set("hex", H(wire::blob(a.hex("data"))));
  } else if (name == "wire.empty_array") {
    r.set("hex", H(wire::empty_array()));
  } else if (name == "wire.u64_array") {
    std::vector<uint64_t> words;
    if (const Value* w = a.get("words"))
      for (const Value& x : w->items()) words.push_back(a.u64(x));
    r.set("hex", H(wire::u64_array(words)));
  } else if (name == "wire.serializer_list") {
    std::vector<Bytes> elems;
    if (const Value* e = a.get("elems")) {
      for (const Value& x : e->items()) {
        std::optional<Bytes> b = x.is_string() ? py::fromhex(x.as_string()) : std::nullopt;
        if (!b) a.ok = false;
        elems.push_back(b ? *b : Bytes());
      }
    }
    r.set("hex", H(wire::serializer_list(elems)));
  } else if (name == "wire.parse_message") {
    Bytes b = a.hex("hex");
    wire::Message m;
    wire::Parsed p = wire::parse_message(b.data(), b.size(), &m);
    r = message_value(p, m);
  } else if (name == "wire.data_chunk") {
    r.set("hex", H(wire::data_chunk(static_cast<uint32_t>(a.i64("transfer")), static_cast<uint32_t>(a.i64("channel")),
                                    static_cast<uint8_t>(a.i64("flags")), a.hex("payload"),
                                    static_cast<uint32_t>(a.i64("index")), static_cast<uint32_t>(a.i64("total")))));
  } else if (name == "wire.data_chunks") {
    Value list = Value::array();
    for (const Bytes& c : wire::data_chunks(static_cast<uint32_t>(a.i64("transfer")),
                                            static_cast<uint32_t>(a.i64("channel")),
                                            static_cast<uint8_t>(a.i64("flags")), a.hex("inner")))
      list.push(H(c));
    r.set("chunks", list);
  } else if (name == "wire.ack") {
    const Value* stamp = a.get("stamp");
    r.set("hex", H(wire::ack(static_cast<uint32_t>(a.i64("transfer")), static_cast<uint32_t>(a.i64("channel")),
                             static_cast<uint8_t>(a.i64("flags")), stamp ? a.u64(*stamp) : 0,
                             static_cast<uint32_t>(a.i64("received")))));
  } else if (name == "wire.ack_confirm") {
    Bytes b = a.hex("hex");
    wire::Message m;
    std::optional<Bytes> c;
    if (wire::parse_message(b.data(), b.size(), &m) == wire::Parsed::kOk) c = wire::ack_confirm(m);
    r.set("hex", c ? H(*c) : Value());
  } else if (name == "wire.inet_aton") {
    std::optional<std::array<uint8_t, 4>> v = wire::inet_aton(a.str("ip"));
    r.set("hex", v ? H(Bytes(v->begin(), v->end())) : Value());
  } else if (name == "wire.resolve_reply") {
    std::optional<Bytes> v = wire::resolve_reply(a.hex("body"), a.str("ip"), a.i64("port"));
    r.set("hex", v ? H(*v) : Value());
  } else if (name == "wire.registry_new") {
    state.registry = std::make_unique<wire::PendingRegistry>(a.i64("max_bytes"),
                                                             static_cast<size_t>(a.i64("max_pending")));
    state.partials.clear();
  } else if (name == "wire.assemble" || name == "wire.release_all") {
    if (!state.registry) state.registry = std::make_unique<wire::PendingRegistry>();
    int64_t id = a.i64("partial");
    std::shared_ptr<wire::Partial>& partial = state.partials[id];
    if (!partial) partial = std::make_shared<wire::Partial>();
    if (name == "wire.assemble") {
      Bytes b = a.hex("hex");
      wire::Message m;
      if (wire::parse_message(b.data(), b.size(), &m) != wire::Parsed::kOk || m.type != wire::T_DATA ||
          !m.total) {
        *result = "wire.assemble needs a DATA message";
        return false;
      }
      auto got = state.registry->assemble(partial, m, static_cast<size_t>(a.i64("chunk")));
      r.set("whole", got.first ? H(*got.first) : Value());
      r.set("count", I(static_cast<int64_t>(got.second)));
    } else {
      state.registry->release_all(*partial);
    }
    r.set("pending", I(state.registry->pending_bytes()));
    r.set("records", I(static_cast<int64_t>(state.registry->records())));
    Value dicts = Value::object();
    for (const auto& p : state.partials) {
      Value keys = Value::array();
      for (const auto& e : p.second->entries) {
        Value k = Value::array();
        k.push(Value::uinteger(e.first.first));
        k.push(Value::uinteger(e.first.second));
        k.push(I(e.second->held));
        Value chunks = Value::array();
        for (const auto& c : e.second->parts) chunks.push(Value::uinteger(c.first));
        k.push(chunks);
        keys.push(k);
      }
      dicts.set(py::int_to_text(p.first), keys);
    }
    r.set("partials", dicts);

  } else if (name == "codec.encode_default") {
    std::vector<std::string> layout = layout_of(a);
    std::map<size_t, Bytes> overrides;
    if (const Value* o = a.get("overrides")) {
      for (const json::Member& m : o->members()) {
        std::optional<std::string> k = py::parse_int(m.key);
        std::optional<Bytes> b = m.value.is_string() ? py::fromhex(m.value.as_string()) : std::nullopt;
        if (!k || !b) a.ok = false;
        if (k && b) overrides[static_cast<size_t>(*py::int_text_to_i64(*k))] = *b;
      }
    }
    std::optional<Bytes> v = codec::encode_default(layout, overrides);
    r.set("hex", v ? H(*v) : Value());
  } else if (name == "codec.decode") {
    std::vector<std::string> layout = layout_of(a);
    std::optional<std::vector<codec::Item>> v = codec::decode(layout, a.hex("body"));
    if (v) {
      Value list = Value::array();
      for (const codec::Item& it : *v) list.push(it.is_bytes ? H(it.bytes) : Value::uinteger(it.value));
      r.set("values", list);
    } else {
      r.set("values", Value());
    }

  } else if (name == "v1_00.align" || name == "v1_00.transcode") {
    std::vector<std::string> l121, l100;
    for (const char* k : {"layout121", "layout100"}) {
      const Value* l = a.get(k);
      std::vector<std::string>& out = std::string(k) == "layout121" ? l121 : l100;
      if (l && l->is_array()) {
        for (const Value& t : l->items()) out.push_back(t.is_string() ? t.as_string() : std::string());
      } else {
        a.ok = false;
      }
    }
    if (name == "v1_00.align") {
      Value list = Value::array();
      for (const std::optional<size_t>& m : v1_00::translate::align(l121, l100)) {
        list.push(m ? Value::integer(static_cast<int64_t>(*m)) : Value());
      }
      r.set("mapping", std::move(list));
    } else {
      Bytes body = a.hex("body");
      Bytes out;
      Raised raised;
      if (v1_00::translate::transcode(body, l121, l100, nullptr, &out, &raised)) {
        r.set("hex", H(out));
      } else {
        r.set("raised", S(raised.line()));
      }
    }

  } else if (name == "aes.encrypt" || name == "aes.decrypt") {
    Bytes key = a.hex("key"), iv = a.hex("iv"), data = a.hex("data");
    std::optional<Bytes> v = name == "aes.encrypt" ? aes::encrypt(key, iv, data) : aes::decrypt(key, iv, data);
    r.set("hex", v ? H(*v) : Value());
  } else if (name == "blowfish.encrypt" || name == "blowfish.decrypt") {
    Bytes key = a.hex("key"), data = a.hex("data");
    std::optional<Bytes> v = name == "blowfish.encrypt" ? blowfish::encrypt(key, data) : blowfish::decrypt(key, data);
    r.set("hex", v ? H(*v) : Value());

  } else if (name == "config.eval") {
    std::optional<Bytes> file = a.hex_or_null("file");
    if (!a.get("file")) a.ok = true;
    py::PosixPaths paths;
    config::Config c(file ? std::optional<std::string>(std::string(file->begin(), file->end())) : std::nullopt,
                     a.str("path"), a.str("home"), paths);
    Value res = Value::object();
    res.set("server_ip_setting", result_value(c.server_ip_setting(), conv_str));
    res.set("advertised_ip", result_value(c.advertised_ip(), conv_opt));
    res.set("server_ip_problem", result_value(c.server_ip_problem(), conv_opt));
    res.set("server_ip", result_value(c.server_ip("192.0.2.1"), conv_str));
    res.set("http_port", result_value(c.http_port(), conv_int));
    res.set("https_port", result_value(c.https_port(), conv_int));
    res.set("tls_cert", result_value(c.tls_cert(), conv_pair));
    res.set("ps4_ip", result_value(c.ps4_ip(), conv_opt));
    res.set("relay_address", result_value(c.relay_address(), conv_pair_int));
    res.set("save_path_configured", result_value(c.save_path_configured(), conv_bool));
    res.set("save_path", result_value(c.save_path(), conv_str));
    res.set("game_data", result_value(c.game_data(), conv_opt));
    res.set("abilities", result_value(c.abilities(), conv_str));
    res.set("crew_credits", result_value(c.crew_credits(), conv_int));
    res.set("crew_credits_refill", result_value(c.crew_credits_refill(), conv_bool));
    res.set("dlc", result_value(c.dlc(), conv_str));
    res.set("unlocks", result_value(c.unlocks(), conv_str));
    res.set("bots", result_value(c.bots(), conv_bool));
    res.set("multiplayer", result_value(c.multiplayer(), conv_str));
    res.set("multiplayer_problem", result_value(c.multiplayer_problem(), conv_opt));
    res.set("afk_kick", result_value(c.afk_kick(), conv_bool));
    res.set("public_set", result_value(c.public_set(), conv_bool));
    res.set("link_home", result_value(c.link_home(), conv_bool));
    res.set("link_home_problem", result_value(c.link_home_problem(), conv_opt));
    res.set("proxy_key", result_value(c.proxy_key(), conv_key));
    r.set("results", res);
    Value start = Value::object();
    std::vector<std::string> lines;
    start.set("check_config", outcome_value(config::check_config(c), lines));
    lines.clear();
    config::Outcome o = config::say_settings(c, &lines);
    start.set("say_settings", outcome_value(o, lines));
    lines.clear();
    o = config::check_server_ip(c, &lines);
    start.set("check_server_ip", outcome_value(o, lines));
    lines.clear();
    o = config::dlc_line(c, &lines);
    start.set("dlc_line", outcome_value(o, lines));
    lines.clear();
    o = config::warn_link_home(c, trusted_v4, &lines);
    start.set("warn_link_home", outcome_value(o, lines));
    lines.clear();
    o = config::warn_multiplayer_public(c, trusted_v4, &lines);
    start.set("warn_multiplayer_public", outcome_value(o, lines));
    r.set("start", start);
  } else if (name == "config.get") {
    std::optional<Bytes> file = a.hex_or_null("file");
    config::Ini ini;
    config::Error err;
    if (!ini.read(file ? std::optional<std::string>(std::string(file->begin(), file->end())) : std::nullopt,
                  a.str("path"), &err)) {
      r.set("read_error", S(err.type + ": " + err.message));
    } else {
      config::Result<std::string> g = ini.get(a.str("section"), a.str("option"));
      r.set("value", g.ok() ? S(*g.value) : Value());
      r.set("error", g.ok() ? Value() : S(g.error.type + ": " + g.error.message));
      r.set("has_option", B(ini.has_option(a.str("section"), a.str("option"))));
    }

  } else if (name == "files.write_atomic") {
    platform::SystemFiles files;
    Bytes data = a.hex("data");
    FileStat st;
    OsError err;
    bool ok = files.write_atomic(a.str("path"), std::string(data.begin(), data.end()), a.flag("private"), -1, -1, &st,
                                 &err);
    r.set("ok", B(ok));
    r.set("mode", I(ok ? static_cast<int64_t>(st.mode) : 0));
    r.set("size", I(ok ? static_cast<int64_t>(st.size) : 0));
    r.set("error", S(ok ? std::string() : err.text()));
  } else if (name == "platform.local_from_ns") {
    plat::LocalTime t = plat::local_from_ns(a.i64("ns"));
    Value parts = Value::array();
    for (int x : {t.year, t.month, t.day, t.hour, t.minute, t.second, t.microsecond}) parts.push(I(x));
    r.set("parts", parts);
  } else if (name == "util.fs_sorted") {
    std::vector<std::string> names;
    for (const Value& n : a.get("names_hex") ? a.get("names_hex")->items() : std::vector<Value>()) {
      Bytes b = py::fromhex(n.as_string()).value_or(Bytes());
      names.emplace_back(b.begin(), b.end());
    }
    std::sort(names.begin(), names.end(), [](const std::string& x, const std::string& y) { return py::fs_name_less(x, y); });
    Value out = Value::array();
    for (const std::string& n : names) out.push(S(py::hex(n)));
    r.set("sorted_hex", out);
  } else if (name == "files.error_type") {
    OsError err;
    err.code = static_cast<int>(a.i64("code"));
    err.winerror = a.flag("winerror");
    r.set("type", S(err.type()));
    r.set("not_found", B(not_found(err)));
  } else if (name == "files.read") {
    platform::SystemFiles files;
    std::string data;
    OsError err;
    bool ok = files.read(a.str("path"), &data, &err);
    r.set("ok", B(ok));
    r.set("hex", ok ? S(py::hex(data)) : Value());
    r.set("error", S(ok ? std::string() : err.text()));

  } else if (name == "session.load") {
    std::optional<Bytes> file = a.hex_or_null("file");
    if (!a.get("file")) a.ok = true;
    session::Loaded l =
        session::load(file ? std::optional<std::string>(std::string(file->begin(), file->end())) : std::nullopt,
                      a.f64("now"));
    Value list = Value::array();
    for (const session::Stored& s : l.sessions) list.push(stored_value(s));
    r.set("sessions", list);
    r.set("error", opt_s(l.error));
  } else if (name == "session.store_text" || name == "session.save") {
    std::vector<session::Stored> sessions;
    const Value* list = a.get("sessions");
    if (list) stored_from(a, *list, &sessions);
    if (name == "session.store_text") {
      r.set("text", S(session::store_text(sessions)));
    } else {
      platform::SystemFiles files;
      session::SessionStore store(files, a.str("path"));
      r.set("message", opt_s(store.save(sessions)));
    }
  } else if (name == "paths.nt" || name == "paths.posix") {
    py::NtPaths nt;
    py::PosixPaths posix;
    const py::PathRules& rules = name == "paths.nt" ? static_cast<const py::PathRules&>(nt) : posix;
    std::string p = a.str("p"), q = a.str("q");
    std::pair<std::string, std::string> parts = rules.split(p);
    r.set("isabs", Value::boolean(rules.isabs(p)));
    r.set("join_hex", S(py::hex(rules.join(p, q))));
    r.set("split_hex", S(py::hex(parts.first) + "|" + py::hex(parts.second)));
    r.set("basename_hex", S(py::hex(rules.basename(p))));
    if (name == "paths.nt") {
      std::string drive, root, tail;
      nt.splitroot(p, &drive, &root, &tail);
      r.set("splitroot_hex", S(py::hex(drive) + "|" + py::hex(root) + "|" + py::hex(tail)));
    }
  } else if (name == "ubi.credential") {
    r.set("r_hex", S(py::hex(ubi::credential(a.str("auth")))));
  } else if (name == "ubi.ticket_name") {
    r.set("r_hex", S(py::hex(ubi::ticket_name(a.str("auth")))));
  } else if (name == "ubi.login_name") {
    const Value* body = a.get("body");
    std::pair<std::string, std::string> who = ubi::login_name(body && body->is_object() ? *body : Value::object(),
                                                              a.str("auth"));
    r.set("name_hex", S(py::hex(who.first)));
    r.set("platform", S(who.second));
  } else if (name == "ubi.trusted") {
    r.set("r", Value::boolean(ubi::trusted(a.str("ip"))));
  } else if (name == "ubi.name_allowed") {
    r.set("r", Value::boolean(ubi::name_allowed(a.str("text"))));
  } else if (name == "ubi.parse_qsl") {
    Value pairs = Value::array();
    for (const auto& kv : ubi::parse_qsl(a.str("qs"))) {
      Value pair = Value::array();
      pair.push(S(py::hex(kv.first)));
      pair.push(S(py::hex(kv.second)));
      pairs.push(pair);
    }
    r.set("pairs_hex", pairs);
  } else if (name == "ubi.b64decode") {
    std::optional<std::string> d = ubi::b64decode_strict(a.str("s"));
    r.set("r", d ? S(py::hex(*d)) : Value());
  } else if (name == "ubi.b64encode") {
    Bytes b = a.hex("data");
    r.set("r", S(ubi::b64encode(std::string(b.begin(), b.end()))));
  } else if (name == "ubi.stable_id") {
    r.set("r", S(ubi::stable_id(a.str("kind"), a.str("text"))));
  } else if (name == "ubi.printable_text") {
    r.set("r_hex", S(py::hex(printable_text(a.str("s"), static_cast<size_t>(a.i64("limit"))))));
  } else if (name == "accounts.account_key") {
    r.set("save_name", S(save_name(a.str("text"))));
    r.set("key", S(account_key(a.str("text"))));
  } else if (name == "scrypt") {
    Bytes pw = a.hex("password"), salt = a.hex("salt");
    std::vector<uint8_t> out;
    bool ok = scrypt(pw.data(), pw.size(), salt.data(), salt.size(), static_cast<uint64_t>(a.i64("n")),
                     static_cast<uint32_t>(a.i64("r")), static_cast<uint32_t>(a.i64("p")),
                     static_cast<size_t>(a.i64("dklen")), &out);
    r.set("r", ok ? S(py::hex(out)) : Value());
  } else if (name == "avatar.decode" || name == "avatar.fit") {
    Bytes data = a.hex("data");
    std::string in(data.begin(), data.end()), out;
    uint32_t w = 0, h = 0;
    Raised raised;
    bool ok = name == "avatar.decode" ? avatar::decode(in, &w, &h, &out, &raised) : avatar::fit(in, &out, &raised);
    if (ok) {
      r.set("width", Value::integer(w));
      r.set("height", Value::integer(h));
      r.set("out", S(py::hex(out)));
    } else {
      r.set("error", S(raised.type));
      r.set("text", S(raised.text));
    }
  } else if (name == "avatar.encode" || name == "avatar.box") {
    Bytes rgba = a.hex("rgba");
    std::string in(rgba.begin(), rgba.end());
    uint32_t w = static_cast<uint32_t>(a.i64("width")), h = static_cast<uint32_t>(a.i64("height"));
    std::string png;
    Raised raised;
    if (name == "avatar.encode") {
      avatar::encode(w, h, in, &png, &raised);
    } else {
      png = avatar::box(w, h, in, static_cast<uint32_t>(a.i64("size")));
    }
    r.set("out", S(py::hex(png)));
  } else if (name == "avatar.check") {
    Bytes data = a.hex("data");
    r.set("r", opt_s(avatar::check_console_picture(std::string(data.begin(), data.end()), a.flag("decode"))));
  } else if (name == "pyobj") {
    obj::Py py;
    const std::string op = a.str("op");
    const Value* x = a.v.get("a");
    const Value* y = a.v.get("b");
    Value va = x ? *x : Value(), vb = y ? *y : Value();
    Value out;
    if (op == "+" || op == "+=") out = py.add(va, vb, op == "+=");
    else if (op == "-" || op == "-=") out = py.sub(va, vb, op == "-=");
    else if (op == "*") out = py.mul(va, vb);
    else if (op == "//") out = py.floordiv(va, vb);
    else if (op == "%") out = py.mod(va, vb);
    else if (op == "<<") out = py.lshift(va, vb);
    else if (op == ">>") out = py.rshift(va, vb);
    else if (op == "&") out = py.band(va, vb);
    else if (op == "|" || op == "|=") out = py.bor(va, vb, op == "|=");
    else if (op == "<") out = Value::boolean(py.lt(va, vb));
    else if (op == "<=") out = Value::boolean(py.le(va, vb));
    else if (op == ">") out = Value::boolean(py.gt(va, vb));
    else if (op == ">=") out = Value::boolean(py.ge(va, vb));
    else if (op == "==") out = Value::boolean(obj::eq(va, vb));
    else if (op == "neg") out = py.neg(va);
    else if (op == "int") out = py.to_int(va);
    else if (op == "float") out = py.to_float(va);
    else if (op == "min") out = py.min(va, vb);
    else if (op == "max") out = py.max(va, vb);
    else if (op == "sum") out = py.sum(va);
    else if (op == "len") out = Value::integer(static_cast<int64_t>(py.len(va)));
    else if (op == "in") out = Value::boolean(py.contains(vb, va));
    else if (op == "getitem") out = py.getitem(va, vb);
    else if (op == "bool") out = Value::boolean(obj::truthy(va));
    else if (op == "repr") out = Value::string(obj::repr(va));
    else if (op == "list") {
      out = Value::array();
      for (Value& v : py.iter(va)) out.push(std::move(v));
    } else if (op == "unpack2") {
      std::vector<Value> xs;
      out = Value::array();
      if (py.unpack(va, 2, &xs)) {
        for (Value& v : xs) out.push(std::move(v));
      }
    } else if (op == "setitem") {
      out = va;
      py.setitem(out, vb, a.v.get("c") ? *a.v.get("c") : Value());
    } else if (op.size() == 5 && op.compare(0, 4, "pack") == 0) {
      Bytes b;
      py.pack(b, op[4], va);
      out = Value::string(py::hex(b));
    }
    if (py.failed()) {
      r.set("raised", S(py.raised().line()));
    } else {
      r.set("r", out);
    }
  } else {
    *result = "unknown debug call " + name;
    return false;
  }
  if (!a.ok) {
    *result = "missing or wrong arguments for " + name;
    return false;
  }
  *result = json::dumps(r);
  return true;
}

namespace {

Value handle_one(v1_21::Ctx& ctx, uint32_t id, const Bytes& body, bool v100) {
  Value r = Value::object();
  v1_21::HandleResult h = v100 ? v1_00::handle(ctx, id, body) : v1_21::handle(ctx, id, body);
  switch (h.kind) {
    case v1_21::HandleResult::kFrame:
      r.set("kind", S("frame"));
      r.set("reply", H(h.frame));
      break;
    case v1_21::HandleResult::kNoReply:
      r.set("kind", S("no_reply"));
      break;
    case v1_21::HandleResult::kUnknown:
      r.set("kind", S("unknown"));
      break;
    case v1_21::HandleResult::kRaised:
      r.set("kind", S("raised"));
      r.set("raised", S(h.raised.line()));
      break;
  }
  return r;
}

void put_states(Value& r, const v1_21::Player& p) {
  r.set("state", S(p.save_text()));
  v1_21::Player copy = p;
  copy.clamp();
  r.set("saved", S(copy.save_text()));
}

}

bool run_v1_21(Core& core, Files& files, const std::string& args_json, std::string* result) {
  std::optional<Value> parsed = json::loads_utf8(args_json);
  if (!parsed || !parsed->is_object()) {
    *result = "arguments are not a JSON object";
    return false;
  }
  Args a{*parsed};
  Value r = Value::object();
  const std::string fn = a.v.get("fn") ? a.str("fn") : "handle";
  Overrides& o = core.overrides();
  if (const Value* v = a.v.get("car_catalog")) o.car_catalog = v->is_bool() && v->as_bool();
  if (const Value* v = a.v.get("reward_tables")) o.reward_tables = v->is_bool() && v->as_bool();
  if (const Value* v = a.v.get("multiplayer")) {
    if (v->is_string()) {
      o.multiplayer = v->as_string();
    } else {
      o.multiplayer.reset();
    }
  }
  if (const Value* v = a.v.get("link_home")) {
    if (v->is_bool()) {
      o.link_home = v->as_bool();
    } else {
      o.link_home.reset();
    }
  }
  if (a.v.get("seed")) core.set_module_random(Random(static_cast<uint64_t>(a.i64("seed"))));
  if (const Value* earned = a.v.get("earned")) {
    std::map<std::string, std::vector<v1_21::Earned>>& windows = core.v1_21_state().earned;
    windows.clear();
    for (const json::Member& m : earned->members()) {
      std::vector<v1_21::Earned> book;
      for (const Value& e : m.value.items()) {
        int64_t t[3] = {0, 0, 0};
        for (size_t i = 0; i < 3 && i < e.items().size(); i++) t[i] = e.items()[i].as_i64().value_or(0);
        book.push_back(v1_21::Earned{t[0], e.items().size() > 1 ? e.items()[1] : Value::integer(0), t[2]});
      }
      windows[m.key] = std::move(book);
    }
  }
  std::string path = a.v.get("path") ? a.str("path") : std::string();
  if (const Value* save = a.v.get("save")) {
    if (save->is_string() && !path.empty()) {
      FileStat st;
      OsError err;
      files.makedirs(files.dirname(path), &err);
      if (!files.write_atomic(path, save->as_string(), false, -1, -1, &st, &err)) {
        *result = "cannot write the save: " + err.text();
        return false;
      }
    }
  }
  if (!a.ok) {
    *result = "missing or wrong arguments for v1_21 " + fn;
    return false;
  }
  Raised raised;
  std::optional<v1_21::Player> p = v1_21::Player::load(files, path, v1_21::reward_max_bucks(core), &raised);
  if (!p) {
    r.set("kind", S("load_raised"));
    r.set("raised", S(raised.line()));
    *result = json::dumps(r);
    return true;
  }
  if (const Value* dlc = a.v.get("dlc")) p->dlc = v1_21::dlc_fields(dlc->is_string() ? dlc->as_string() : "all");
  if (const Value* cc = a.v.get("cc_fixed")) p->cc_fixed = cc->as_i64().value_or(0);
  v1_21::Ctx ctx;
  ctx.core = &core;
  if (a.v.get("player_name")) ctx.player_name = a.str("player_name");
  ctx.player_id = v1_21::player_id_for(ctx.player_name);
  if (const Value* id = a.v.get("player_id")) ctx.player_id = a.u64(*id);
  if (const Value* now = a.v.get("now_us")) ctx.fixed_now_us = now->as_i64();
  if (const Value* g = a.v.get("crew_group")) ctx.crew_group = a.u64(*g);
  if (const Value* pc = a.v.get("pc")) ctx.pc = pc->is_bool() && pc->as_bool();
  if (const Value* verified = a.v.get("verified")) ctx.verified = verified->is_bool() && verified->as_bool();
  if (const Value* ip = a.v.get("client_ip")) ctx.client_ip = ip->is_string() ? ip->as_string() : "";
  ctx.player = std::move(*p);
  const bool v100 = a.v.get("game") && a.str("game") == "01.00";
  if (fn == "load") {
    r.set("kind", S("loaded"));
  } else if (fn == "handle") {
    if (const Value* requests = a.v.get("requests")) {
      const bool fresh = a.v.get("fresh") && a.v.get("fresh")->is_bool() && a.v.get("fresh")->as_bool();
      const Value* save = a.v.get("save");
      Value steps = Value::array();
      for (const Value& q : requests->items()) {
        if (fresh) {
          if (save && save->is_string() && !path.empty()) {
            FileStat st;
            OsError err;
            files.write_atomic(path, save->as_string(), false, -1, -1, &st, &err);
          }
          core.v1_21_state().earned.clear();
          if (const Value* seed = q.get("seed")) core.set_module_random(Random(seed->as_u64().value_or(0)));
          Raised again;
          std::optional<v1_21::Player> p2 = v1_21::Player::load(files, path, v1_21::reward_max_bucks(core), &again);
          if (!p2) {
            Value step = Value::object();
            step.set("kind", S("load_raised"));
            step.set("raised", S(again.line()));
            steps.push(std::move(step));
            continue;
          }
          if (const Value* dlc = a.v.get("dlc")) {
            p2->dlc = v1_21::dlc_fields(dlc->is_string() ? dlc->as_string() : "all");
          }
          if (const Value* cc = a.v.get("cc_fixed")) p2->cc_fixed = cc->as_i64().value_or(0);
          ctx.player = std::move(*p2);
        }
        const Value* id = q.get("request");
        const Value* body = q.get("body");
        const Value* now = q.get("now_us");
        std::optional<Bytes> b = body && body->is_string() ? py::fromhex(body->as_string()) : std::nullopt;
        if (!id || !id->as_u64() || !b || !now || !now->as_i64()) {
          *result = "missing or wrong arguments for v1_21 handle (requests)";
          return false;
        }
        ctx.fixed_now_us = *now->as_i64();
        Value step = handle_one(ctx, static_cast<uint32_t>(*id->as_u64()), *b, v100);
        put_states(step, ctx.player);
        if (fresh && !path.empty()) {
          std::string text;
          OsError err;
          step.set("file", files.exists(path) && files.read(path, &text, &err) ? S(text) : Value());
        }
        steps.push(std::move(step));
      }
      r.set("steps", std::move(steps));
    } else {
      const Value* req = a.get("request");
      uint32_t id = req ? static_cast<uint32_t>(a.u64(*req)) : 0;
      Bytes body = a.hex("body");
      if (!a.ok) {
        *result = "missing or wrong arguments for v1_21 handle";
        return false;
      }
      r = handle_one(ctx, id, body, v100);
    }
  } else if (fn == "physparts.give" || fn == "physparts.mission_part") {
    const Value* model = a.get("model");
    const Value* spec = a.get("spec");
    uint64_t m = model ? a.u64(*model) : 0, s = spec ? a.u64(*spec) : 0;
    if (fn == "physparts.give") {
      std::vector<int64_t> parts;
      if (const Value* list = a.get("parts")) {
        for (const Value& x : list->items()) parts.push_back(x.as_i64().value_or(0));
      }
      bool mount = a.flag("mount"), given = false;
      if (!a.ok) {
        *result = "missing or wrong arguments for v1_21 " + fn;
        return false;
      }
      if (v1_21::physparts::give(ctx.player, m, s, parts, mount, &given, &raised)) {
        r.set("kind", S("result"));
        r.set("r", B(given));
      } else {
        r.set("kind", S("raised"));
        r.set("raised", S(raised.line()));
      }
    } else {
      const Value* mission = a.get("mission");
      uint64_t mi = mission ? a.u64(*mission) : 0;
      int64_t medal = a.i64("medal");
      if (!a.ok) {
        *result = "missing or wrong arguments for v1_21 " + fn;
        return false;
      }
      std::optional<int64_t> part;
      if (v1_21::physparts::mission_part(ctx.player, mi, medal, m, s, core.module_random(), core, &part, &raised)) {
        r.set("kind", S("result"));
        r.set("r", part ? I(*part) : Value());
      } else {
        r.set("kind", S("raised"));
        r.set("raised", S(raised.line()));
      }
    }
  } else {
    *result = "unknown v1_21 call " + fn;
    return false;
  }
  if (!r.get("steps")) put_states(r, ctx.player);
  r.set("crew_group", Value::uinteger(ctx.crew_group));
  Value windows = Value::object();
  for (const auto& e : core.v1_21_state().earned) windows.set(e.first, earned_value(e.second));
  r.set("earned", std::move(windows));
  r.set("player_earned", earned_value(ctx.player.earned));
  if (a.v.get("draw_random")) r.set("next_random", Value::number(core.module_random().random()));
  *result = json::dumps(r);
  return true;
}

}
}
