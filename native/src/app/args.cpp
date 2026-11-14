#include "app/args.h"

#include "core/util.h"
#include "platform/platform.h"

namespace tcr::app {

namespace {

const char kExtras[] = "\x01extras";

void print_error(const ParserSpec& spec, const std::string& message) {
  std::string usage = spec.usage;
  if (!usage.empty() && usage.back() == '\n') usage.pop_back();
  plat::print_error_line(usage);
  plat::print_error_line(spec.prog + ": error: " + message);
}

void print_text(const std::string& text) {
  std::string t = text;
  if (!t.empty() && t.back() == '\n') t.pop_back();
  plat::print_line(t);
}

bool negative_number(const std::string& a) {
  if (a.size() < 2 || a[0] != '-') return false;
  size_t i = 1, digits = 0;
  while (i < a.size() && a[i] >= '0' && a[i] <= '9') ++i, ++digits;
  if (i == a.size()) return digits > 0;
  if (a[i] != '.') return false;
  ++i;
  size_t frac = 0;
  while (i < a.size() && a[i] >= '0' && a[i] <= '9') ++i, ++frac;
  return i == a.size() && frac > 0;
}

const OptionSpec* exact_option(const ParserSpec& spec, const std::string& name) {
  for (const OptionSpec& o : spec.options) {
    for (const std::string& n : o.names) {
      if (n == name) return &o;
    }
  }
  return nullptr;
}

bool option_tuples(const ParserSpec& spec, const std::string& a) {
  std::string prefix = a.substr(0, a.find('='));
  bool long_form = a.size() > 1 && a[1] == '-';
  for (const OptionSpec& o : spec.options) {
    for (const std::string& n : o.names) {
      if (!long_form && n == a.substr(0, 2)) return true;
      if (n.compare(0, prefix.size(), prefix) == 0) return true;
    }
  }
  return false;
}

bool looks_like_option(const ParserSpec& spec, const std::string& a) {
  if (a.empty() || a[0] != '-') return false;
  if (exact_option(spec, a)) return true;
  if (a.size() == 1) return false;
  size_t eq = a.find('=');
  if (eq != std::string::npos && exact_option(spec, a.substr(0, eq))) return true;
  if (option_tuples(spec, a)) return true;
  if (negative_number(a)) return false;
  return a.find(' ') == std::string::npos;
}

std::string display(const OptionSpec& o) {
  std::string out;
  for (const std::string& n : o.names) out += (out.empty() ? "" : "/") + n;
  return out;
}

bool convert(const ParserSpec& spec, const OptionSpec& o, const std::string& value, int* exit_code) {
  if (o.type == OptionSpec::kInt && !py::parse_int(value)) {
    *exit_code = arg_error(spec, "argument " + display(o) + ": invalid int value: " + py::repr(value));
    return false;
  }
  if (o.type == OptionSpec::kFloat && !py::parse_float(value)) {
    *exit_code = arg_error(spec, "argument " + display(o) + ": invalid float value: " + py::repr(value));
    return false;
  }
  return true;
}

const OptionSpec* find_option(const ParserSpec& spec, const std::string& name, std::string* ambiguous) {
  for (const OptionSpec& o : spec.options) {
    for (const std::string& n : o.names) {
      if (n == name) return &o;
    }
  }
  if (name.compare(0, 2, "--") != 0) return nullptr;
  std::vector<std::pair<const OptionSpec*, std::string>> fits;
  for (const OptionSpec& o : spec.options) {
    for (const std::string& n : o.names) {
      if (n.compare(0, 2, "--") == 0 && n.compare(0, name.size(), name) == 0) fits.push_back({&o, n});
    }
  }
  if (fits.size() == 1) return fits[0].first;
  if (fits.size() > 1) {
    std::string list;
    for (const auto& f : fits) list += (list.empty() ? "" : ", ") + f.second;
    *ambiguous = list;
  }
  return nullptr;
}

}

int arg_error(const ParserSpec& spec, const std::string& message) {
  print_error(spec, message);
  return 2;
}

std::optional<Parsed> parse_args(const ParserSpec& spec, const std::vector<std::string>& args, int* exit_code) {
  Parsed out;
  std::vector<std::string> extras;
  std::vector<std::string> positional;
  size_t i = 0;
  bool only_positional = false;
  bool dash_pending = false;
  bool after_positional = false;
  for (; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (!only_positional && a == "--") {
      only_positional = true;
      if (!after_positional) dash_pending = true;
      continue;
    }
    if (only_positional || !looks_like_option(spec, a)) {
      if (!spec.commands.empty() && positional.size() == spec.positionals.size()) {
        const ParserSpec* sub = nullptr;
        std::string choices;
        for (const auto& c : spec.commands) {
          choices += (choices.empty() ? "" : ", ") + c.first;
          if (c.first == a) sub = c.second;
        }
        if (!sub) {
          *exit_code = arg_error(spec, "argument " + spec.command_dest + ": invalid choice: " + py::repr(a) +
                                           " (choose from " + choices + ")");
          return std::nullopt;
        }
        dash_pending = false;
        out.command = a;
        std::vector<std::string> rest(args.begin() + static_cast<std::ptrdiff_t>(i) + 1, args.end());
        std::optional<Parsed> inner = parse_args(*sub, rest, exit_code);
        if (!inner) return std::nullopt;
        auto extra = inner->values.find(kExtras);
        if (extra != inner->values.end()) {
          *exit_code = arg_error(spec, "unrecognized arguments: " + extra->second);
          return std::nullopt;
        }
        for (auto& v : inner->values) out.values[v.first] = v.second;
        for (auto& f : inner->flags) out.flags[f.first] += f.second;
        i = args.size();
        break;
      }
      if (positional.size() < spec.positionals.size()) {
        positional.push_back(a);
        dash_pending = false;
        after_positional = true;
      } else {
        if (dash_pending) extras.push_back("--");
        dash_pending = false;
        extras.push_back(a);
        after_positional = false;
      }
      continue;
    }
    after_positional = false;
    if (a[1] != '-') {
      std::string opt = a, explicit_arg;
      bool has_explicit = false, sep_eq = false;
      size_t eq = a.find('=');
      if (!exact_option(spec, a)) {
        if (a.substr(0, eq) == "-") {
          std::string list;
          for (const OptionSpec& o : spec.options)
            for (const std::string& n : o.names) list += (list.empty() ? "" : ", ") + n;
          *exit_code = arg_error(spec, "ambiguous option: " + a + " could match " + list);
          return std::nullopt;
        }
        if (eq != std::string::npos && exact_option(spec, a.substr(0, eq))) {
          opt = a.substr(0, eq);
          explicit_arg = a.substr(eq + 1);
          has_explicit = sep_eq = true;
        } else if (exact_option(spec, a.substr(0, 2))) {
          opt = a.substr(0, 2);
          explicit_arg = a.substr(2);
          has_explicit = true;
        } else {
          extras.push_back(a);
          continue;
        }
      }
      std::vector<const OptionSpec*> taken;
      for (;;) {
        const OptionSpec* o = exact_option(spec, opt);
        if (o->kind == OptionSpec::kValue) {
          std::string value;
          if (has_explicit) {
            value = explicit_arg;
          } else if (i + 1 < args.size() && !looks_like_option(spec, args[i + 1]) && args[i + 1] != "--") {
            value = args[++i];
          } else {
            *exit_code = arg_error(spec, "argument " + display(*o) + ": expected one argument");
            return std::nullopt;
          }
          if (!convert(spec, *o, value, exit_code)) return std::nullopt;
          out.values[o->dest] = value;
          break;
        }
        if (!has_explicit) {
          taken.push_back(o);
          break;
        }
        if (explicit_arg.empty() || sep_eq || explicit_arg[0] == '-') {
          *exit_code = arg_error(spec, "argument " + display(*o) + ": ignored explicit argument " +
                                           py::repr(explicit_arg));
          return std::nullopt;
        }
        taken.push_back(o);
        std::string next = std::string("-") + explicit_arg[0];
        if (!exact_option(spec, next)) {
          extras.push_back("-" + explicit_arg);
          break;
        }
        opt = next;
        explicit_arg = explicit_arg.substr(1);
        has_explicit = !explicit_arg.empty();
        sep_eq = has_explicit && explicit_arg[0] == '=';
        if (sep_eq) explicit_arg = explicit_arg.substr(1);
      }
      for (const OptionSpec* o : taken) {
        if (o->kind == OptionSpec::kHelp) {
          print_text(spec.help);
          *exit_code = 0;
          return std::nullopt;
        }
        if (o->kind == OptionSpec::kVersion) {
          print_text(spec.version);
          *exit_code = 0;
          return std::nullopt;
        }
        out.flags[o->dest] += 1;
      }
      continue;
    }
    std::string name = a;
    std::optional<std::string> inline_value;
    size_t eq = a.find('=');
    if (eq != std::string::npos) {
      name = a.substr(0, eq);
      inline_value = a.substr(eq + 1);
    }
    std::string ambiguous;
    const OptionSpec* o = find_option(spec, name, &ambiguous);
    if (!o) {
      if (!ambiguous.empty()) {
        *exit_code = arg_error(spec, "ambiguous option: " + a + " could match " + ambiguous);
        return std::nullopt;
      }
      extras.push_back(a);
      continue;
    }
    if (o->kind != OptionSpec::kValue) {
      if (inline_value) {
        *exit_code = arg_error(spec, "argument " + display(*o) + ": ignored explicit argument " +
                                         py::repr(*inline_value));
        return std::nullopt;
      }
      if (o->kind == OptionSpec::kHelp) {
        print_text(spec.help);
        *exit_code = 0;
        return std::nullopt;
      }
      if (o->kind == OptionSpec::kVersion) {
        print_text(spec.version);
        *exit_code = 0;
        return std::nullopt;
      }
      out.flags[o->dest] += 1;
      continue;
    }
    std::string value;
    if (inline_value) {
      value = *inline_value;
    } else if (i + 1 < args.size() && !looks_like_option(spec, args[i + 1]) && args[i + 1] != "--") {
      value = args[++i];
    } else {
      *exit_code = arg_error(spec, "argument " + display(*o) + ": expected one argument");
      return std::nullopt;
    }
    if (!convert(spec, *o, value, exit_code)) return std::nullopt;
    out.values[o->dest] = value;
  }
  std::string missing;
  for (size_t k = positional.size(); k < spec.positionals.size() && k < spec.required; ++k) {
    missing += (missing.empty() ? "" : ", ") + spec.positionals[k];
  }
  if (!spec.commands.empty() && out.command.empty()) missing += (missing.empty() ? "" : ", ") + spec.command_dest;
  if (!missing.empty()) {
    *exit_code = arg_error(spec, "the following arguments are required: " + missing);
    return std::nullopt;
  }
  for (size_t k = 0; k < positional.size(); ++k) out.values[spec.positionals[k]] = positional[k];
  if (dash_pending && positional.size() >= spec.positionals.size()) extras.push_back("--");
  if (!extras.empty()) {
    std::string list;
    for (const std::string& e : extras) list += (list.empty() ? "" : " ") + e;
    if (spec.subcommand) {
      out.values[kExtras] = list;
      return out;
    }
    *exit_code = arg_error(spec, "unrecognized arguments: " + list);
    return std::nullopt;
  }
  return out;
}

}
