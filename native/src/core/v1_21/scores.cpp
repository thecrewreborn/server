#include "core/v1_21/scores.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "core/core.h"
#include "core/files.h"
#include "core/util.h"
#include "core/v1_21/module_state.h"
#include "core/v1_21/rewards.h"

namespace tcr {
namespace v1_21 {
namespace scores {

namespace {

using json::Value;

bool board_key(const std::string& k, uint64_t* out) {
  if (k.size() < 3 || k.size() > 18 || k[0] != '0' || k[1] != 'x') return false;
  if (k[2] == '0' && k.size() > 3) return false;
  uint64_t v = 0;
  for (size_t i = 2; i < k.size(); i++) {
    char c = k[i];
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (d < 0) return false;
    v = v << 4 | static_cast<uint64_t>(d);
  }
  *out = v;
  return true;
}

bool entry_of(const Value& e, Entry* out) {
  if (!e.is_array() || e.items().size() != 3) return false;
  for (const Value& x : e.items()) {
    if (!x.is_int()) return false;
  }
  std::optional<uint64_t> player = e.items()[0].as_u64();
  std::optional<int64_t> score = e.items()[1].as_i64();
  std::optional<int64_t> at = e.items()[2].as_i64();
  if (!player || !score || !at || *score <= 0 || *score >= U32 || *at < 0) return false;
  *out = Entry{*player, *score, *at};
  return true;
}

void sort_board(std::vector<Entry>& board, bool lower) {
  std::stable_sort(board.begin(), board.end(), [lower](const Entry& a, const Entry& b) {
    int64_t ka = lower ? a.score : -a.score, kb = lower ? b.score : -b.score;
    return ka != kb ? ka < kb : a.at < b.at;
  });
}

std::string key_of(uint64_t id) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(id));
  return buf;
}

}

bool lower_better(const Core& core, uint64_t activity) {
  const rewards::Tables* t = rewards::tables(core);
  return t && t->lower_better.count(activity);
}

bool known(const Core& core, uint64_t activity) {
  const rewards::Tables* t = rewards::tables(core);
  return !t || t->missions.count(activity) || t->activities.count(activity);
}

Boards boards_of(const Core& core, const Value& d) {
  Boards out;
  if (!d.is_object()) return out;
  for (const json::Member& m : d.members()) {
    uint64_t activity = 0;
    if (!board_key(m.key, &activity) || !m.value.is_array() || activity < PVP_KEYS) continue;
    std::vector<Entry> valid;
    for (const Value& e : m.value.items()) {
      Entry one;
      if (entry_of(e, &one)) valid.push_back(one);
    }
    sort_board(valid, lower_better(core, activity));
    std::set<uint64_t> seen;
    std::vector<Entry> board;
    for (const Entry& e : valid) {
      if (seen.insert(e.player).second) board.push_back(e);
    }
    if (!board.empty()) out[activity] = std::move(board);
  }
  return out;
}

Boards load(Core& core) {
  Files* files = core.files();
  if (!files) return Boards();
  std::string text, decoded;
  OsError err;
  if (!files->read(path(core), &text, &err) || decode_text_file(files, text, true, &decoded)) return Boards();
  std::optional<Value> v = json::loads(py::universal_newlines(decoded), json::kFileMaxDepth);
  if (!v) return Boards();
  return boards_of(core, *v);
}

std::string dumps(const Boards& boards) {
  Value out = Value::object();
  for (const auto& b : boards) {
    Value list = Value::array();
    for (const Entry& e : b.second) {
      Value row = Value::array();
      row.push(Value::uinteger(e.player));
      row.push(Value::integer(e.score));
      row.push(Value::integer(e.at));
      list.push(std::move(row));
    }
    out.set(key_of(b.first), std::move(list));
  }
  return json::dumps(out);
}

std::vector<Entry> board(Core& core, uint64_t activity) {
  if (activity < PVP_KEYS) return {};
  Boards boards = load(core);
  auto it = boards.find(activity);
  return it == boards.end() ? std::vector<Entry>() : it->second;
}

bool record(Core& core, uint64_t activity, uint64_t player, int64_t score, int64_t at_us) {
  if (score == 0 || score == U32 || activity < PVP_KEYS || !known(core, activity)) return false;
  const bool lower = lower_better(core, activity);
  Boards boards = load(core);
  std::vector<Entry>& entries = boards[activity];
  auto own = std::find_if(entries.begin(), entries.end(), [player](const Entry& e) { return e.player == player; });
  if (own != entries.end() && !(lower ? score < own->score : score > own->score)) return false;
  if (own != entries.end()) entries.erase(own);
  entries.push_back(Entry{player, score, at_us});
  sort_board(entries, lower);
  Files* files = core.files();
  if (!files) return false;
  FileStat st;
  OsError err;
  return files->write_atomic(path(core), dumps(boards), false, -1, -1, &st, &err);
}

std::string path(Core& core) {
  ModuleState& m = core.v1_21_state();
  if (!m.scores_path) {
    const py::PathRules& paths = py::native_paths();
    config::Result<bool> own = core.config().save_path_configured();
    std::string save = paths.join(core.home(), "save");
    m.scores_path = own.ok() && *own.value ? paths.join(save, "scores.json")
                                           : paths.join(paths.join(save, "1.21"), "scores.json");
  }
  return *m.scores_path;
}

}
}
}
