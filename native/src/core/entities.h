#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/json.h"

namespace tcr {
class Core;
struct OsError;
namespace entities {

constexpr size_t kObjMax = 8192;
constexpr size_t kObjDepth = 32;
constexpr size_t kTagsMax = 32;
constexpr size_t kTagMax = 64;
constexpr int64_t kRevisionMax = 2147483647;

struct Entry {
  std::string type;
  std::optional<std::string> space;
  json::Value tags;
  json::Value obj;
  int64_t revision = 1;
  std::string last_modified;
  std::optional<std::string> upload_key;
};

struct File {
  std::optional<std::string> profile;
  std::map<std::string, Entry> entities;
};

std::string new_upload_key(Core& core);
std::optional<uint64_t> ghost_id(const std::string& name);
bool is_ghost(const std::string& name);
bool accepted(const Core& core, const std::string& name);
bool guid_ok(const json::Value* v);
bool guid_ok(const std::string& s);
std::string entity_id(const std::string& key, const std::string& name);
bool obj_ok(const json::Value* obj);
bool tags_ok(const json::Value* tags);

std::string folder(Core& core);
std::string path(Core& core, const std::string& stem);
std::string blob_path(Core& core, const std::string& stem, const std::string& eid);
File load(Core& core, const std::string& stem);
std::string dumps(const std::string& owner, const std::string& profile, const std::map<std::string, Entry>& entities,
                  const std::string& key);
bool save(Core& core, const std::string& stem, const std::string& owner, const std::string& profile,
          const std::map<std::string, Entry>& entities, OsError* err);
std::optional<std::string> find(const std::string& stem, const File& file, const std::string& eid);
std::vector<std::string> stems(Core& core);
struct Located {
  std::string stem;
  File file;
  std::string name;
};
std::optional<Located> locate(Core& core, const std::string& eid);

}
}
