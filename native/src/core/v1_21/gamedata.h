#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/json.h"
#include "core/util.h"

namespace tcr {
namespace v1_21 {
namespace gamedata {

struct AutoReply {
  uint32_t reply = 0;
  std::vector<std::string> layout;
};
const std::vector<std::pair<uint32_t, AutoReply>>& auto_replies();
const AutoReply* auto_reply(uint32_t request);

struct ReplyTemplate {
  uint32_t request = 0;
  std::string name;
  uint32_t type = 0;
  Bytes body;
  std::vector<std::pair<size_t, std::string>> fields;
};
const std::vector<ReplyTemplate>& reply_templates();
const ReplyTemplate* reply_template(uint32_t request);

const std::vector<std::pair<int, Bytes>>& dlc_fields();

struct SummitRecord {
  std::array<int64_t, 4> start{};
  std::array<int64_t, 4> end{};
  bool monthly = false;
  std::vector<std::string> events;
};
struct Summit {
  std::vector<SummitRecord> records;
};
const Summit* summit();

const json::Value* table(const std::string& name);

uint64_t hex_id(const std::string& text);
std::string hex_key(uint64_t id);

int64_t int_of(const json::Value* v);
double float_of(const json::Value* v);
std::string str_of(const json::Value* v);
std::vector<int64_t> ints_of(const json::Value* v);
uint64_t hex_of(const json::Value* v);

}
}
}
