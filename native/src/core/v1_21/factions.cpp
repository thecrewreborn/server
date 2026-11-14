#include "core/v1_21/factions.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/core.h"
#include "core/util.h"
#include "core/v1_21/module_state.h"
#include "core/v1_21/summit.h"

namespace tcr {
namespace v1_21 {
namespace factions {

const int64_t START[GROUPS][FIELD_COUNT] = {
    {22940, 5158, 9831, 15281, 19243, 22940, 6000, 12846, 1, 6193, 50, 2523, 815, 802, 2037, 206, 4075, 0, 8150, 0},
    {7729, 1315, 2576, 4451, 5757, 7729, 64912, 4328, 1, 2086, 30, 850, 304, 270, 760, 69, 1521, 0, 3042, 0},
    {14465, 3021, 5834, 8622, 10857, 14465, 44740, 8100, 1, 3905, 47, 1591, 479, 506, 1197, 130, 2395, 0, 4791, 0},
    {9269, 1679, 3480, 5448, 7424, 9269, 61728, 5190, 1, 2502, 33, 1019, 331, 324, 829, 83, 1659, 0, 3318, 0},
    {16507, 3596, 7107, 9321, 13078, 16507, 29968, 9243, 1, 4456, 50, 1815, 607, 577, 1517, 148, 3035, 0, 6071, 0},
};

namespace {

using obj::F;
using obj::I;
using obj::Py;
using obj::V;

V idx(int off) { return I(static_cast<int64_t>(index_of(off))); }

void add_to(Py& py, V& rec, int off, const V& x) {
  V grown = py.add(py.getitem(rec, idx(off)), x, true);
  py.setitem(rec, idx(off), std::move(grown));
}

void tick(Py& py, V& rec, Random& rng) {
  int64_t total = 0;
  for (int off : {0x54, 0x58, 0x5C, 0x60, 0x64}) {
    total += rng.randint(6, 19).value_or(6);
    add_to(py, rec, off, I(total));
    if (py.failed()) return;
  }
  py.setitem(rec, idx(0x50), py.getitem(rec, idx(0x64)));
  const std::pair<int, double> shares[] = {{0x6C, 0.56}, {0x74, 0.27}, {0x7C, 0.11}};
  for (const auto& s : shares) {
    V have = py.getitem(rec, idx(s.first));
    if (py.failed()) return;
    double x = static_cast<double>(total) * s.second + rng.random();
    py.setitem(rec, idx(s.first), py.add(have, py.to_int(F(x)), true));
  }
  {
    V have = py.getitem(rec, idx(0x84));
    if (py.failed()) return;
    py.setitem(rec, idx(0x84), py.add(have, I(rng.randint(1, 3).value_or(1)), true));
  }
  {
    V have = py.getitem(rec, idx(0x8C));
    if (py.failed()) return;
    py.setitem(rec, idx(0x8C), py.add(have, obj::B(rng.random() < 0.5), true));
  }
  if (py.failed()) return;
  py.setitem(rec, idx(0x68), I(rng.randint(6000, 65000).value_or(6000)));
  py.setitem(rec, idx(0x78), I(rng.randint(25, 50).value_or(25)));
  if (py.failed()) return;
  V v84 = py.getitem(rec, idx(0x84));
  if (py.failed()) return;
  py.setitem(rec, idx(0x80), py.to_int(py.mul(v84, F(rng.uniform(0.55, 1.05)))));
  py.setitem(rec, idx(0x88), py.to_int(py.mul(py.getitem(rec, idx(0x80)), F(2.5))));
  py.setitem(rec, idx(0x90), py.mul(py.getitem(rec, idx(0x88)), I(2)));
  py.setitem(rec, idx(0x98), py.mul(py.getitem(rec, idx(0x90)), I(2)));
}

V start_state(int64_t now) {
  V groups = V::array();
  for (const auto& rec : START) {
    V r = V::array();
    for (int64_t v : rec) r.push(I(v));
    groups.push(std::move(r));
  }
  V state = V::object();
  state.set("time", I(now));
  state.set("groups", std::move(groups));
  return state;
}

}

V field(Py& py, const V& rec, int off) { return py.getitem(rec, idx(off)); }

std::string path(Core& core) {
  ModuleState& m = core.v1_21_state();
  if (!m.factions_path) {
    const py::PathRules& paths = py::native_paths();
    config::Result<bool> own = core.config().save_path_configured();
    std::string save = paths.join(core.home(), "save");
    m.factions_path = own.ok() && *own.value ? paths.join(save, "factions.json")
                                             : paths.join(paths.join(save, "1.21"), "factions.json");
  }
  return *m.factions_path;
}

std::optional<Groups> load(Core& core, int64_t now, Raised* raised) {
  Files* files = core.files();
  if (!files) {
    *raised = Raised{"OSError", "no file system"};
    return std::nullopt;
  }
  const std::string file = path(core);
  V state;
  bool loaded = false;
  std::string text;
  OsError err;
  std::string decoded;
  if (files->read(file, &text, &err) && !decode_text_file(files, text, true, &decoded)) {
    json::Failure why = json::Failure::kSyntax;
    std::optional<V> v = json::loads(py::universal_newlines(decoded), json::kFileMaxDepth, &why);
    if (v) {
      state = std::move(*v);
      loaded = true;
    } else if (why != json::Failure::kSyntax) {
      *raised = Raised{"RecursionError", json::recursion_text(why)};
      return std::nullopt;
    }
  }
  if (!loaded) state = start_state(now);
  Py py;
  auto failed = [&]() {
    *raised = py.raised();
    return std::nullopt;
  };
  const V time = py.getitem(state, V::string("time"));
  V hours = py.min(py.max(I(0), py.floordiv(py.sub(I(now), time), I(HOUR))), I(MAX_CATCH_UP));
  if (py.failed()) return failed();
  if (obj::truthy(hours)) {
    Random rng = core.env().unseeded_random();
    std::optional<int64_t> n = obj::small_int(hours);
    if (!n) {
      py.raise("TypeError", std::string("'") + obj::type_name(hours) + "' object cannot be interpreted as an integer");
      return failed();
    }
    for (int64_t h = 0; h < *n; h++) {
      V* groups = state.is_object() ? state.get("groups") : nullptr;
      if (!groups) {
        py.getitem(state, V::string("groups"));
        return failed();
      }
      std::vector<V> recs = py.iter(*groups);
      if (py.failed()) return failed();
      if (groups->is_array()) {
        for (V& rec : groups->items()) {
          tick(py, rec, rng);
          if (py.failed()) return failed();
        }
      } else {
        for (V& rec : recs) {
          tick(py, rec, rng);
          if (py.failed()) return failed();
        }
      }
    }
    V moved = py.lt(hours, I(MAX_CATCH_UP)) ? py.add(time, py.mul(hours, I(HOUR))) : I(now);
    if (py.failed()) return failed();
    state.set("time", std::move(moved));
  }
  if (obj::truthy(hours) || !files->exists(file)) {
    FileStat st;
    if (!files->makedirs(files->dirname(file), &err) ||
        !files->write_atomic(file, json::dumps(state), false, -1, -1, &st, &err)) {
      *raised = Raised{err.type(), err.text()};
      return std::nullopt;
    }
  }
  V groups = py.getitem(state, V::string("groups"));
  if (py.failed()) return failed();
  return groups;
}

}
}
}
