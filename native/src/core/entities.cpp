#include "core/entities.h"

#include <algorithm>
#include <utility>

#include "core/core.h"
#include "core/files.h"
#include "core/ubi.h"
#include "core/util.h"
#include "core/v1_21/rewards.h"

namespace tcr {
namespace entities {

namespace {

using json::Value;

const char* const kSettings[] = {"playersettings", "playerpreferences"};

bool hex_digit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

std::string lower_ascii(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

bool stem_ok(const std::string& s) {
  if (s.empty() || s.size() > 32) return false;
  for (char c : s) {
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

bool upload_key_ok(const std::string& s) {
  if (s.size() != 32) return false;
  for (char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool depth_ok(const Value& root, size_t limit) {
  std::vector<std::pair<const Value*, size_t>> stack{{&root, 1}};
  while (!stack.empty()) {
    auto [x, d] = stack.back();
    stack.pop_back();
    if (x->is_object() || x->is_array()) {
      if (d > limit) return false;
      if (x->is_object()) {
        for (const json::Member& m : x->members()) stack.emplace_back(&m.value, d + 1);
      } else {
        for (const Value& v : x->items()) stack.emplace_back(&v, d + 1);
      }
    }
  }
  return true;
}

std::optional<Entry> entry_of(const Core& core, const std::string& name, const Value& e) {
  if (!accepted(core, name) || !e.is_object()) return std::nullopt;
  const Value* kind = e.get("type");
  const Value* tags = e.get("tags");
  const Value* obj = e.get("obj");
  const Value* rev = e.get("revision");
  const Value* stamp = e.get("lastModified");
  if (!kind || !kind->is_string() || (is_ghost(name) && kind->as_string() != "ghost") || !tags_ok(tags) ||
      !obj_ok(obj) || !rev || !rev->is_int() || !stamp || !stamp->is_string()) {
    return std::nullopt;
  }
  std::optional<int64_t> r = rev->as_i64();
  if (!r || *r < 1 || *r > kRevisionMax) return std::nullopt;
  Entry out;
  out.type = kind->as_string();
  if (const Value* space = e.get("spaceId")) {
    if (!guid_ok(space)) return std::nullopt;
    out.space = space->as_string();
  }
  out.tags = *tags;
  out.obj = *obj;
  out.revision = *r;
  out.last_modified = stamp->as_string();
  const Value* key = e.get("uploadKey");
  if (is_ghost(name) && key && key->is_string() && upload_key_ok(key->as_string())) out.upload_key = key->as_string();
  return out;
}

}

std::string new_upload_key(Core& core) {
  uint8_t raw[16];
  core.env().urandom(raw, sizeof(raw));
  return py::hex(raw, sizeof(raw));
}

std::optional<uint64_t> ghost_id(const std::string& name) {
  const std::string head = "ghost_";
  if (name.size() <= head.size() || name.compare(0, head.size(), head) != 0) return std::nullopt;
  std::string digits = name.substr(head.size());
  if (digits.size() > 16 || (digits[0] == '0' && digits.size() > 1)) return std::nullopt;
  uint64_t v = 0;
  for (char c : digits) {
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (d < 0) return std::nullopt;
    v = v << 4 | static_cast<uint64_t>(d);
  }
  return v;
}

bool is_ghost(const std::string& name) { return ghost_id(name).has_value(); }

bool accepted(const Core& core, const std::string& name) {
  for (const char* s : kSettings) {
    if (name == s) return true;
  }
  std::optional<uint64_t> activity = ghost_id(name);
  if (!activity) return false;
  const v1_21::rewards::Tables* t = v1_21::rewards::tables(core);
  return !t || t->activities.count(*activity);
}

bool guid_ok(const std::string& s) {
  if (s.size() != 36) return false;
  for (size_t i = 0; i < s.size(); i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-') return false;
    } else if (!hex_digit(s[i])) {
      return false;
    }
  }
  return s != "00000000-0000-0000-0000-000000000000";
}

bool guid_ok(const Value* v) { return v && v->is_string() && guid_ok(v->as_string()); }

std::string entity_id(const std::string& key, const std::string& name) {
  return ubi::stable_id("entity", key + ":" + name);
}

bool obj_ok(const Value* obj) {
  return obj && obj->is_object() && depth_ok(*obj, kObjDepth) && json::dumps_compact(*obj).size() <= kObjMax;
}

bool tags_ok(const Value* tags) {
  if (!tags || !tags->is_array() || tags->items().size() > kTagsMax) return false;
  for (const Value& t : tags->items()) {
    if (!t.is_string() || t.size() > kTagMax) return false;
  }
  return true;
}

std::string folder(Core& core) {
  std::optional<std::string>& cached = core.entities_folder();
  if (!cached) {
    const py::PathRules& paths = py::native_paths();
    config::Result<bool> own = core.config().save_path_configured();
    std::string save = paths.join(core.home(), "save");
    cached = own.ok() && *own.value ? paths.join(save, "entities") : paths.join(paths.join(save, "1.21"), "entities");
  }
  return *cached;
}

std::string path(Core& core, const std::string& stem) { return py::native_paths().join(folder(core), stem + ".json"); }

std::string blob_path(Core& core, const std::string& stem, const std::string& eid) {
  const py::PathRules& paths = py::native_paths();
  return paths.join(paths.join(folder(core), stem), eid + ".bin");
}

File load(Core& core, const std::string& stem) {
  File out;
  Files* files = core.files();
  if (!files) return out;
  std::string text, decoded;
  OsError err;
  if (!files->read(path(core, stem), &text, &err) || decode_text_file(files, text, true, &decoded)) return out;
  std::optional<Value> d = json::loads(py::universal_newlines(decoded), json::kHttpMaxDepth);
  if (!d || !d->is_object()) return out;
  if (guid_ok(d->get("profileId"))) out.profile = d->get("profileId")->as_string();
  const Value* ents = d->get("entities");
  if (ents && ents->is_object()) {
    for (const json::Member& m : ents->members()) {
      if (m.int_key) continue;
      std::optional<Entry> e = entry_of(core, m.key, m.value);
      if (e) out.entities[m.key] = std::move(*e);
    }
  }
  return out;
}

std::string dumps(const std::string& owner, const std::string& profile, const std::map<std::string, Entry>& entities,
                  const std::string& key) {
  Value ents = Value::object();
  for (const auto& [name, e] : entities) {
    Value v = Value::object();
    v.set("entityId", Value::string(entity_id(key, name)));
    v.set("type", Value::string(e.type));
    if (e.space) v.set("spaceId", Value::string(*e.space));
    v.set("tags", e.tags);
    v.set("obj", e.obj);
    v.set("revision", Value::integer(e.revision));
    v.set("lastModified", Value::string(e.last_modified));
    if (e.upload_key) v.set("uploadKey", Value::string(*e.upload_key));
    ents.set(name, std::move(v));
  }
  Value out = Value::object();
  out.set("profileId", Value::string(profile));
  out.set("name", Value::string(owner));
  out.set("entities", std::move(ents));
  return json::dumps(out);
}

bool save(Core& core, const std::string& stem, const std::string& owner, const std::string& profile,
          const std::map<std::string, Entry>& entities, OsError* err) {
  Files* files = core.files();
  if (!files) return false;
  FileStat st;
  return files->write_atomic(path(core, stem), dumps(owner, profile, entities, lower_ascii(stem)), false, -1, -1, &st,
                             err);
}

std::optional<std::string> find(const std::string& stem, const File& file, const std::string& eid) {
  const std::string key = lower_ascii(stem);
  for (const auto& kv : file.entities) {
    if (entity_id(key, kv.first) == eid) return kv.first;
  }
  return std::nullopt;
}

std::vector<std::string> stems(Core& core) {
  std::vector<std::string> out;
  Files* files = core.files();
  std::vector<std::string> names;
  OsError err;
  if (!files || !files->listdir(folder(core), &names, &err)) return out;
  for (const std::string& n : names) {
    if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0 && stem_ok(n.substr(0, n.size() - 5))) {
      out.push_back(n.substr(0, n.size() - 5));
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::optional<Located> locate(Core& core, const std::string& eid) {
  for (const std::string& stem : stems(core)) {
    File f = load(core, stem);
    std::optional<std::string> name = find(stem, f, eid);
    if (name) return Located{stem, std::move(f), *name};
  }
  return std::nullopt;
}

}
}
