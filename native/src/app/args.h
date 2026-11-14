#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tcr::app {

struct OptionSpec {
  std::vector<std::string> names;
  std::string dest;
  enum Kind { kValue, kFlag, kHelp, kVersion } kind = kValue;
  enum Type { kText, kInt, kFloat } type = kText;
};

struct ParserSpec {
  std::string prog;
  std::string usage;
  std::string help;
  std::string version;
  std::vector<OptionSpec> options;
  std::vector<std::string> positionals;
  size_t required = static_cast<size_t>(-1);
  std::string command_dest;
  std::vector<std::pair<std::string, const ParserSpec*>> commands;
  bool subcommand = false;
};

struct Parsed {
  std::map<std::string, std::string> values;
  std::map<std::string, int> flags;
  std::string command;
  bool has(const std::string& dest) const { return values.count(dest) != 0; }
  std::string get(const std::string& dest, const std::string& fallback = "") const {
    auto it = values.find(dest);
    return it == values.end() ? fallback : it->second;
  }
};

std::optional<Parsed> parse_args(const ParserSpec& spec, const std::vector<std::string>& args, int* exit_code);

int arg_error(const ParserSpec& spec, const std::string& message);

}
