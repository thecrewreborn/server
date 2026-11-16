#include <algorithm>
#include <unordered_map>

#include "core/core.h"
#include "core/v1_21/handlers.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/scores.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {

namespace {

constexpr uint64_t NO_ID = 0xFFFFFFFFFFFFFFFFull;
constexpr size_t IDS_AT = 20;
constexpr uint32_t MAX_PLAYERS = 101;
constexpr size_t MAX_BOARDS = 1600;
constexpr int64_t WINDOW = 24;
constexpr int64_t HALF = WINDOW / 2;

void append(Bytes& out, const Bytes& more) { out.insert(out.end(), more.begin(), more.end()); }

Bytes array_of(const std::vector<Bytes>& items) {
  Bytes body;
  wire::put_u32(body, static_cast<uint32_t>(items.size()));
  for (const Bytes& b : items) append(body, b);
  return wire::blob(wire::frame(wire::T_ARRAY, body));
}

HandlerResult get_all_scores(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised r;
  if (!pack::unpack_check(body, IDS_AT + 4 + 12, 4, &r)) return HandlerResult::fail(r);
  uint32_t count = std::min(wire::get_u32(body.data() + IDS_AT + 4 + 12), MAX_PLAYERS);
  const size_t at = IDS_AT + 4 + 16;
  if (!pack::unpack_check(body, at, 8 * static_cast<size_t>(count), &r)) return HandlerResult::fail(r);
  std::vector<uint64_t> ids;
  for (uint32_t i = 0; i < count; i++) ids.push_back(wire::get_u64(body.data() + at + 8 * i));
  const scores::Boards boards = scores::load(*ctx.core);
  std::vector<uint64_t> activities;
  std::vector<Bytes> elems;
  for (const auto& b : boards) {
    const std::vector<scores::Entry>& board = b.second;
    std::unordered_map<uint64_t, int64_t> best;
    for (const scores::Entry& e : board) best.emplace(e.player, e.score);
    if (!std::any_of(ids.begin(), ids.end(), [&best](uint64_t i) { return best.count(i) > 0; })) continue;
    if (activities.size() == MAX_BOARDS) break;
    activities.push_back(b.first);
    std::vector<Bytes> column;
    for (uint64_t i : ids) {
      auto it = best.find(i);
      Bytes one;
      wire::put_u32(one, it == best.end() ? 0 : static_cast<uint32_t>(it->second));
      column.push_back(std::move(one));
    }
    Bytes elem = array_of(column);
    wire::put_u64(elem, board.empty() ? NO_ID : board[0].player);
    wire::put_u32(elem, board.empty() ? 0 : static_cast<uint32_t>(board[0].score));
    elems.push_back(std::move(elem));
  }
  Bytes out{1};
  append(out, wire::u64_array(activities));
  append(out, array_of(elems));
  return HandlerResult::reply(0x08D85D01, std::move(out));
}

Bytes page(const std::vector<scores::Entry>& board, int64_t start, int64_t stop) {
  const int64_t n = static_cast<int64_t>(board.size());
  std::vector<Bytes> rows;
  for (int64_t rank = std::min(start, n); rank < std::min(stop, n); rank++) {
    Bytes row;
    wire::put_u64(row, board[static_cast<size_t>(rank)].player);
    wire::put_u32(row, static_cast<uint32_t>(rank));
    wire::put_u32(row, static_cast<uint32_t>(board[static_cast<size_t>(rank)].score));
    rows.push_back(std::move(row));
  }
  Bytes out{1};
  wire::put_u32(out, static_cast<uint32_t>(board.size()));
  append(out, array_of(rows));
  return out;
}

HandlerResult leaderboard_for_player(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised r;
  if (!pack::unpack_check(body, 0, 24, &r)) return HandlerResult::fail(r);
  uint64_t activity = wire::get_u64(body.data());
  uint64_t player = wire::get_u64(body.data() + 16);
  const std::vector<scores::Entry> board = scores::board(*ctx.core, activity);
  auto it = std::find_if(board.begin(), board.end(), [player](const scores::Entry& e) { return e.player == player; });
  int64_t start = 0, stop = WINDOW;
  if (it != board.end()) {
    int64_t pos = it - board.begin();
    start = std::max<int64_t>(0, pos - HALF);
    stop = pos + HALF + 1;
  }
  return HandlerResult::reply(0x09D47633, page(board, start, stop));
}

HandlerResult leaderboard_for_rank(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised r;
  if (!pack::unpack_check(body, 0, 20, &r)) return HandlerResult::fail(r);
  uint64_t activity = wire::get_u64(body.data());
  int64_t start = wire::get_u32(body.data() + 16);
  return HandlerResult::reply(0xB40DCDA8, page(scores::board(*ctx.core, activity), start, start + WINDOW));
}

void add(std::vector<Registered>& out, uint32_t id, const char* name, Handler fn) {
  Registered reg;
  reg.id = id;
  reg.name = name;
  reg.fn = fn;
  out.push_back(std::move(reg));
}

}

void register_scores(std::vector<Registered>& out) {
  add(out, 0xF76B6FF1, "GET_ALL_SCORES", get_all_scores);
  add(out, 0x62793B3A, "LEADERBOARD_PLAYER", leaderboard_for_player);
  add(out, 0x28FBB2DE, "LEADERBOARD_RANK", leaderboard_for_rank);
}

}
}
