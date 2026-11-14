#include <cstdio>
#include <string>
#include <vector>

#include "app/args.h"
#include "app/server.h"
#include "app/system.h"
#include "app/test_hooks.h"
#include "core/discovery.h"
#include "core/server_id.h"
#include "core/util.h"
#include "core/version.h"
#include "platform/platform.h"

namespace tcr::app {

namespace {

const char kUsage[] =
    "usage: TheCrewReborn-Server console-password [-h] [--server-id SERVER_ID]\n"
    "                                             key [server]\n";

const char kHelp[] =
    "usage: TheCrewReborn-Server console-password [-h] [--server-id SERVER_ID]\n"
    "                                             key [server]\n"
    "\n"
    "Print the PC password for a name claimed with a PS4 on one server. The server\n"
    "is asked for its id on UDP port 4001; the HTTPS port in SERVER (default 443)\n"
    "is part of the code.\n"
    "\n"
    "positional arguments:\n"
    "  key                   console_key.txt from the PS4 (/data/TheCrew), or its\n"
    "                        32 hex characters\n"
    "  server                the server exactly as in server.txt: host, host:port\n"
    "                        or https://host[:port]\n"
    "\n"
    "options:\n"
    "  -h, --help            show this help message and exit\n"
    "  --server-id SERVER_ID\n"
    "                        the server's id (32 hex characters, server_id of\n"
    "                        /version) instead of asking it; the server's address\n"
    "                        is needed all the same\n";

struct Failed {
  bool no_server_id = false;
  std::string text;
};

bool key_line(const std::string& raw) {
  if (raw.size() < 32) return false;
  for (size_t i = 0; i < 32; ++i) {
    char c = raw[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  std::string end = raw.substr(32);
  return end.empty() || end == "\n" || end == "\r\n";
}

bool read_key(const std::string& arg, std::string* key, Failed* f) {
  std::string raw, source;
  if (plat::is_file(arg)) {
    plat::Error err;
    if (!plat::read_file(arg, &raw, &err)) {
      f->text = plat::python_error_text(err) + ": " + py::repr(arg);
      return false;
    }
    if (raw.size() > 64) raw.resize(64);
    source = "the key file";
  } else {
    raw = py::strip(arg);
    source = "the key";
  }
  if (!key_line(raw)) {
    f->text = source + " is not 32 lowercase hex characters (the PS4 makes a new key for such a file)";
    return false;
  }
  *key = raw.substr(0, 32);
  return true;
}

std::string first_line(const std::string& text) {
  std::string out;
  for (uint32_t cp : py::code_points(py::strip(text))) {
    bool line_end = cp == '\n' || cp == '\r' || cp == 0x0B || cp == 0x0C || cp == 0x1C || cp == 0x1D || cp == 0x1E ||
                    cp == 0x85 || cp == 0x2028 || cp == 0x2029;
    if (line_end) break;
    py::append_utf8(out, cp);
  }
  return py::strip(out);
}

std::string without_scheme(const std::string& line) {
  auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  if (line.empty() || !alpha(line[0])) return line;
  size_t i = 1;
  while (i < line.size() && (alpha(line[i]) || (line[i] >= '0' && line[i] <= '9') || line[i] == '+' ||
                             line[i] == '.' || line[i] == '-')) {
    ++i;
  }
  return line.compare(i, 3, "://") == 0 ? line.substr(i + 3) : line;
}

std::string before(const std::string& s, char c) { return s.substr(0, s.find(c)); }

bool host_of(const std::string& text, std::string* host, Failed* f) {
  *host = before(before(without_scheme(first_line(text)), '/'), ':');
  if (host->empty()) {
    f->text = "no server address in " + py::repr(py::strip(text));
    return false;
  }
  return true;
}

bool port_of(const std::string& text, int* port, Failed* f) {
  std::string line = before(without_scheme(first_line(text)), '/');
  size_t colon = line.find(':');
  if (colon == std::string::npos) {
    *port = 443;
    return true;
  }
  std::string p = line.substr(colon + 1);
  bool digits = !p.empty() && p.size() <= 5;
  for (char c : p) digits = digits && c >= '0' && c <= '9';
  int value = digits ? std::atoi(p.c_str()) : 0;
  if (!digits || value < 1 || value > 65535) {
    f->text = "no HTTPS port in " + py::repr(py::strip(text));
    return false;
  }
  *port = value;
  return true;
}

bool plain_http(const std::string& text) {
  std::vector<uint32_t> cps = py::code_points(text);
  size_t i = 0;
  while (i < cps.size() && py::isspace(cps[i])) ++i;
  const char* want = "http://";
  for (size_t k = 0; want[k]; ++k, ++i) {
    if (i >= cps.size()) return false;
    uint32_t c = cps[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c != static_cast<unsigned char>(want[k])) return false;
  }
  return true;
}

std::string dotted(const std::string& b, size_t at) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", static_cast<unsigned char>(b[at]), static_cast<unsigned char>(b[at + 1]),
                static_cast<unsigned char>(b[at + 2]), static_cast<unsigned char>(b[at + 3]));
  return buf;
}

bool fetch_answer(const std::string& address, std::string* reply, Failed* f) {
  int port = discovery_port();
  plat::Error err;
  plat::Socket s = plat::udp_open("0.0.0.0", 0, false, false, &err);
  if (s == plat::kNoSocket) {
    f->text = plat::python_error_text(err);
    return false;
  }
  plat::udp_report_connreset(s);
  std::string probe = std::string(discovery::PROBE) + "tool " + kVersion;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (plat::udp_sendto(s, probe.data(), probe.size(), plat::Addr{address, port}, &err) < 0) {
      f->text = plat::python_error_text(err);
      plat::close_socket(s);
      return false;
    }
    while (plat::wait_readable(s, 1.0, &err) > 0) {
      char buf[512];
      plat::Addr from;
      long n = plat::udp_recvfrom(s, buf, sizeof(buf), &from, &err);
      if (n < 0) {
        f->text = plat::python_error_text(err);
        plat::close_socket(s);
        return false;
      }
      std::string data(buf, static_cast<size_t>(n));
      if (from.ip == address && data.compare(0, 6, discovery::ANSWER) == 0) {
        *reply = data;
        plat::close_socket(s);
        return true;
      }
    }
  }
  plat::close_socket(s);
  f->text = "no answer from " + address + " on UDP port " + std::to_string(port) +
            " (the server's discovery port must be reachable, or give --server-id)";
  return false;
}

int fail(const Failed& f) {
  if (f.no_server_id) {
    say_error("the server sent no server id: it is older than 0.0.4, and PS4 patches send it no key at all");
  } else {
    say_error("error: " + f.text);
  }
  return 2;
}

}

int run_console_password(const std::vector<std::string>& argv) {
  ParserSpec spec;
  spec.prog = "TheCrewReborn-Server console-password";
  spec.usage = kUsage;
  spec.help = kHelp;
  spec.options = {{{"-h", "--help"}, "help", OptionSpec::kHelp, OptionSpec::kText},
                  {{"--server-id"}, "server_id", OptionSpec::kValue, OptionSpec::kText}};
  spec.positionals = {"key", "server"};
  spec.required = 1;
  int code = 0;
  std::optional<Parsed> args = parse_args(spec, argv, &code);
  if (!args) return code;

  Failed f;
  std::string key, host, address;
  int port = 443;
  if (!read_key(args->get("key"), &key, &f)) return fail(f);
  if (!args->has("server")) {
    f.text = "give the server's address (the code is bound to it)";
    return fail(f);
  }
  const std::string server = args->get("server");
  if (plain_http(server)) {
    f.text = "a plain-HTTP server (http://) gets no code from the PS4 (it sends OFFLINE); use HTTPS";
    return fail(f);
  }
  if (!host_of(server, &host, &f)) return fail(f);
  if (!plat::resolve_ipv4(host, discovery::DISCOVERY_PORT, &address, &f.text)) return fail(f);
  if (!port_of(server, &port, &f)) return fail(f);
  if (address.compare(0, 4, "127.") == 0) {
    say_error("note: " + address +
              " is a loopback address. The PS4 binds its code to the address it talks to: the one in its server.txt, "
              "else the address the server announces on the LAN. Give that address, not localhost.");
  }
  Bytes sid;
  if (args->has("server_id")) {
    std::string error;
    std::optional<Bytes> id = py::fromhex(py::strip(args->get("server_id")), &error);
    if (!id) {
      f.text = error;
      return fail(f);
    }
    if (id->size() != kServerIdSize) {
      f.text = "--server-id must be 32 hex characters";
      return fail(f);
    }
    sid = *id;
  } else {
    std::string reply;
    if (!fetch_answer(address, &reply, &f)) return fail(f);
    if (reply.compare(0, 6, discovery::ANSWER) != 0 || reply.size() < 17 + kServerIdSize) {
      f.no_server_id = true;
      return fail(f);
    }
    sid.assign(reply.begin() + 17, reply.begin() + 17 + kServerIdSize);
    std::string announced = dotted(reply, 6);
    if (announced != address) {
      say_error("note: the server announces " + announced +
                ". A PS4 that found it on the LAN (no server.txt) talks to that address; for such a console run this "
                "again with " + announced + ".");
    }
    int https = (static_cast<unsigned char>(reply[15]) << 8) | static_cast<unsigned char>(reply[16]);
    if (https != 0 && https != port) {
      say_error("note: the server announces HTTPS port " + std::to_string(https) +
                ". A PS4 that found it on the LAN (no server.txt) talks to that port; for such a console run this "
                "again with " + announced + ":" + std::to_string(https) + ".");
    }
  }
  std::optional<Bytes> key_bytes = py::fromhex(key);
  std::optional<std::string> code_text = console_code(*key_bytes, sid, address, port);
  say(code_text.value_or(""));
  return 0;
}

}
