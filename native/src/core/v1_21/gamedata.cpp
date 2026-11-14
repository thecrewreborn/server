#include "core/v1_21/gamedata.h"

#include <cstdio>
#include <memory>
#include <unordered_map>

#include "core/embedded.h"

namespace tcr {
namespace v1_21 {
namespace gamedata {

namespace {

struct Parsed {
  std::map<std::string, json::Value> tables;
  Parsed() {
    for (size_t i = 0; i < embedded::kTableCount; i++) {
      const embedded::Table& t = embedded::kTables[i];
      std::optional<json::Value> v = json::loads_utf8(reinterpret_cast<const uint8_t*>(t.data), t.size);
      if (v) tables.emplace(t.name, std::move(*v));
    }
  }
};

const Parsed& parsed() {
  static const Parsed p;
  return p;
}

}

int64_t int_of(const json::Value* v) {
  if (!v) return 0;
  if (v->is_bool()) return v->as_bool() ? 1 : 0;
  if (v->is_float()) return static_cast<int64_t>(v->as_float());
  std::optional<int64_t> i = v->as_i64();
  return i ? *i : 0;
}

double float_of(const json::Value* v) {
  if (!v) return 0.0;
  if (v->is_float()) return v->as_float();
  if (v->is_bool()) return v->as_bool() ? 1.0 : 0.0;
  std::optional<int64_t> i = v->as_i64();
  return i ? static_cast<double>(*i) : 0.0;
}

std::string str_of(const json::Value* v) { return v && v->is_string() ? v->as_string() : std::string(); }

std::vector<int64_t> ints_of(const json::Value* v) {
  std::vector<int64_t> out;
  if (v && v->is_array()) {
    for (const json::Value& x : v->items()) out.push_back(int_of(&x));
  }
  return out;
}

std::string hex_key(uint64_t id) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(id));
  return buf;
}

uint64_t hex_of(const json::Value* v) { return v && v->is_string() ? hex_id(v->as_string()) : 0; }

const json::Value* table(const std::string& name) {
  const auto& t = parsed().tables;
  auto it = t.find(name);
  return it == t.end() ? nullptr : &it->second;
}

uint64_t hex_id(const std::string& text) {
  size_t i = 0;
  if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) i = 2;
  uint64_t v = 0;
  for (; i < text.size(); i++) {
    char c = text[i];
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) continue;
    v = v * 16 + static_cast<uint64_t>(d);
  }
  return v;
}

const std::vector<std::pair<uint32_t, AutoReply>>& auto_replies() {
  static const std::vector<std::pair<uint32_t, AutoReply>> list = [] {
    std::vector<std::pair<uint32_t, AutoReply>> out;
    const json::Value* t = table("auto_replies.json");
    if (!t || !t->is_object()) return out;
    for (const json::Member& m : t->members()) {
      AutoReply a;
      a.reply = static_cast<uint32_t>(hex_id(str_of(m.value.get("reply"))));
      if (const json::Value* layout = m.value.get("layout")) {
        for (const json::Value& tok : layout->items()) a.layout.push_back(tok.as_string());
      }
      out.emplace_back(static_cast<uint32_t>(hex_id(m.key)), std::move(a));
    }
    return out;
  }();
  return list;
}

const AutoReply* auto_reply(uint32_t request) {
  static const std::unordered_map<uint32_t, size_t> index = [] {
    std::unordered_map<uint32_t, size_t> out;
    const auto& list = auto_replies();
    for (size_t i = 0; i < list.size(); i++) out[list[i].first] = i;
    return out;
  }();
  auto it = index.find(request);
  return it == index.end() ? nullptr : &auto_replies()[it->second].second;
}

const std::vector<ReplyTemplate>& reply_templates() {
  static const std::vector<ReplyTemplate> list = [] {
    std::vector<ReplyTemplate> out;
    const json::Value* t = table("replies.json");
    if (!t || !t->is_object()) return out;
    for (const json::Member& m : t->members()) {
      ReplyTemplate r;
      r.request = static_cast<uint32_t>(hex_id(m.key));
      r.name = str_of(m.value.get("name"));
      r.type = static_cast<uint32_t>(hex_id(str_of(m.value.get("type"))));
      std::optional<Bytes> body = py::fromhex(str_of(m.value.get("body")));
      if (body) r.body = std::move(*body);
      if (const json::Value* fields = m.value.get("fields")) {
        for (const json::Value& f : fields->items()) {
          if (f.items().size() < 2) continue;
          r.fields.emplace_back(static_cast<size_t>(int_of(&f.items()[0])), f.items()[1].as_string());
        }
      }
      out.push_back(std::move(r));
    }
    return out;
  }();
  return list;
}

const ReplyTemplate* reply_template(uint32_t request) {
  for (const ReplyTemplate& r : reply_templates()) {
    if (r.request == request) return &r;
  }
  return nullptr;
}

const std::vector<std::pair<int, Bytes>>& dlc_fields() {
  static const std::vector<std::pair<int, Bytes>> list = [] {
    std::vector<std::pair<int, Bytes>> out;
    const json::Value* t = table("dlc.json");
    const json::Value* fields = t ? t->get("fields") : nullptr;
    if (!fields || !fields->is_object()) return out;
    for (const json::Member& m : fields->members()) {
      std::optional<Bytes> b = py::fromhex(str_of(&m.value));
      std::optional<int64_t> k = py::int_text_to_i64(py::parse_int(m.key).value_or("0"));
      out.emplace_back(static_cast<int>(k.value_or(0)), b.value_or(Bytes()));
    }
    return out;
  }();
  return list;
}

const Summit* summit() {
  static const std::unique_ptr<Summit> data = []() -> std::unique_ptr<Summit> {
    const json::Value* t = table("summit.json");
    if (!t || !t->is_object()) return nullptr;
    std::unique_ptr<Summit> s(new Summit());
    if (const json::Value* records = t->get("records")) {
      for (const json::Value& r : records->items()) {
        SummitRecord rec;
        const json::Value* start = r.get("start");
        const json::Value* end = r.get("end");
        for (size_t i = 0; i < 4; i++) {
          if (start && i < start->items().size()) rec.start[i] = int_of(&start->items()[i]);
          if (end && i < end->items().size()) rec.end[i] = int_of(&end->items()[i]);
        }
        const json::Value* monthly = r.get("monthly");
        rec.monthly = monthly && monthly->is_bool() && monthly->as_bool();
        if (const json::Value* events = r.get("events")) {
          for (const json::Value& e : events->items()) rec.events.push_back(e.as_string());
        }
        s->records.push_back(std::move(rec));
      }
    }
    return s;
  }();
  return data.get();
}

}
}
}
