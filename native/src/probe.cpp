#include "probe.h"

#include <cstdio>
#include <map>

#include "core/core.h"
#include "core/json.h"
#include "core/v1_00/handlers.h"
#include "core/v1_21/handlers.h"

namespace tcr {
namespace v1_21 {

namespace {

class MemoryFiles final : public Files {
 public:
  bool read(const std::string& path, std::string* data, OsError* err) override {
    auto it = files_.find(path);
    if (it == files_.end()) {
      *err = missing(path);
      return false;
    }
    *data = it->second;
    return true;
  }
  bool write_atomic(const std::string& path, std::string_view data, bool, int64_t, int64_t, FileStat* st,
                    OsError*) override {
    files_[path] = std::string(data);
    *st = FileStat();
    st->size = data.size();
    return true;
  }
  bool stat(const std::string& path, FileStat* st, OsError* err) override {
    auto it = files_.find(path);
    if (it == files_.end()) {
      *err = missing(path);
      return false;
    }
    *st = FileStat();
    st->size = it->second.size();
    return true;
  }
  bool read_stat(const std::string& path, std::string* data, FileStat* st, OsError* err) override {
    return read(path, data, err) && stat(path, st, err);
  }
  bool makedirs(const std::string&, OsError*) override { return true; }
  bool replace(const std::string& src, const std::string& dst, OsError* err) override {
    auto it = files_.find(src);
    if (it == files_.end()) {
      *err = missing(src);
      return false;
    }
    files_[dst] = it->second;
    files_.erase(src);
    return true;
  }
  bool rename(const std::string& src, const std::string& dst, OsError* err) override { return replace(src, dst, err); }
  bool exists(const std::string& path) override { return files_.count(path) != 0; }
  bool lexists(const std::string& path) override { return exists(path); }
  bool listdir(const std::string&, std::vector<std::string>* names, OsError*) override {
    names->clear();
    return true;
  }

 private:
  static OsError missing(const std::string& path) {
    OsError e;
    e.code = 2;
    e.message = "No such file or directory";
    e.filename = path;
    return e;
  }
  std::map<std::string, std::string> files_;
};

std::string class_name(const std::string& type) {
  size_t dot = type.rfind('.');
  return dot == std::string::npos ? type : type.substr(dot + 1);
}

}

std::string probe_json(Core& core) {
  std::vector<uint32_t> ids;
  for (const Registered& r : registry()) ids.push_back(r.id);
  return probe_json(core, ids, handle);
}

std::string probe_json(Core& core, const std::vector<uint32_t>& ids, ProbeHandle fn) {
  json::Value out = json::Value::object();
  for (uint32_t id : ids) {
    MemoryFiles files;
    Raised raised;
    std::optional<Player> p = Player::load(files, "/probe/p.json", reward_max_bucks(core), &raised);
    Ctx ctx;
    ctx.core = &core;
    ctx.player_id = 0x1122334455667788ull;
    ctx.fixed_now_us = 1700000000000000ll;
    ctx.player = std::move(*p);
    ctx.player.bucks = obj::I(1234);
    ctx.player.crew_credits = 56;
    ctx.player.level = 3;
    ctx.player.xp = obj::I(789);
    HandleResult res = fn(ctx, id, Bytes(0x100, 0));
    std::string value;
    switch (res.kind) {
      case HandleResult::kFrame:
      case HandleResult::kNoReply:
        value = py::hex(res.frame);
        break;
      case HandleResult::kUnknown:
        value = "None";
        break;
      case HandleResult::kRaised:
        value = py::hex("error " + class_name(res.raised.type));
        break;
    }
    char key[16];
    std::snprintf(key, sizeof(key), "0x%08x", id);
    out.set(key, json::Value::string(value));
  }
  return json::dumps(out);
}

}

namespace v1_00 {

std::string probe_json(Core& core) { return v1_21::probe_json(core, probe_ids(), handle); }

}
}
