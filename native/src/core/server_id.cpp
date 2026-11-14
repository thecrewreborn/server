#include "core/server_id.h"

#include <algorithm>

#include "core/wire.h"
#include "mbedtls/md.h"

namespace tcr {

namespace {

const char kFreeWith[] =
    "then every name a PS4 claimed on this server must be freed with tools/accounts.py and claimed again). "
    "The server does not start.";

bool number_less(const std::string& a, const std::string& b) {
  return a.size() != b.size() ? a.size() < b.size() : a < b;
}

std::string join_names(const std::vector<std::string>& names) {
  std::string out;
  for (size_t i = 0; i < names.size(); i++) out += (i ? ", " : "") + names[i];
  return out;
}

}

bool ServerId::bad_files(std::vector<std::string>* out, OsError* err) {
  auto [folder, name] = paths_.split(path_);
  std::vector<std::string> names;
  out->clear();
  if (!files_.listdir(folder.empty() ? "." : folder, &names, err)) return not_found(*err);
  std::string bad = name + ".bad";
  std::vector<std::string> numbers;
  bool plain = false;
  for (const std::string& n : names) {
    if (n == bad) plain = true;
    if (n.size() > bad.size() + 1 && n.compare(0, bad.size(), bad) == 0 && n[bad.size()] == '.') {
      std::string digits = n.substr(bad.size() + 1);
      bool ok = digits[0] >= '1' && digits[0] <= '9' &&
                std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; });
      if (ok) numbers.push_back(digits);
    }
  }
  std::sort(numbers.begin(), numbers.end(), number_less);
  if (plain) out->push_back(paths_.join(folder, bad));
  for (const std::string& d : numbers) out->push_back(paths_.join(folder, bad + "." + d));
  return true;
}

std::optional<Bytes> ServerId::get(bool* damaged, std::string* error) {
  if (damaged) *damaged = false;
  if (cache_) return cache_;
  auto fail = [&](bool is_damaged, const std::string& text) -> std::optional<Bytes> {
    if (damaged) *damaged = is_damaged;
    if (error) *error = text;
    return std::nullopt;
  };
  std::string raw;
  OsError err;
  std::optional<Bytes> value;
  bool missing = false;
  if (!files_.read_head(path_, 4 * kServerIdSize, &raw, &err)) {
    if (!not_found(err)) return fail(false, err.text());
    missing = true;
  } else {
    bool ascii = std::all_of(raw.begin(), raw.end(), [](char c) { return static_cast<uint8_t>(c) < 0x80; });
    std::optional<Bytes> parsed = ascii ? py::fromhex(py::strip(raw)) : std::nullopt;
    value = parsed && parsed->size() == kServerIdSize ? parsed : Bytes();
  }
  if (!missing && value->empty()) {
    std::string bad = path_ + ".bad";
    for (uint64_t i = 1; files_.lexists(bad); i++) bad = path_ + ".bad." + std::to_string(i);
    if (!files_.rename(path_, bad, &err)) return fail(false, err.text());
    return fail(true, path_ + " is damaged and was kept as " + bad + ". Restore " + path_ +
                          " from your backup, or delete the damaged file to start with a new server id (" + kFreeWith);
  }
  if (missing) {
    std::vector<std::string> kept;
    if (!bad_files(&kept, &err)) return fail(false, err.text());
    if (!kept.empty()) {
      std::string list = join_names(kept);
      return fail(true, path_ + " is missing and " + list + " exists from an earlier damaged file. Restore " + path_ +
                            " from your backup, or delete " + list + " to start with a new server id (" + kFreeWith);
    }
    Bytes fresh(kServerIdSize);
    env_.urandom(fresh.data(), fresh.size());
    FileStat st;
    if (!files_.write_atomic(path_, py::hex(fresh) + "\n", true, -1, -1, &st, &err)) return fail(false, err.text());
    value = fresh;
  }
  cache_ = value;
  return value;
}

std::optional<std::string> ServerId::hex_id(bool* damaged, std::string* error) {
  std::optional<Bytes> v = get(damaged, error);
  if (!v) return std::nullopt;
  return py::hex(*v);
}

std::optional<std::string> console_code(const Bytes& key, const Bytes& server, const std::string& address, int port) {
  std::optional<std::array<uint8_t, 4>> ip = wire::inet_aton(address);
  if (!ip || port < 0 || port > 65535) return std::nullopt;
  Bytes msg = server;
  msg.insert(msg.end(), ip->begin(), ip->end());
  msg.push_back(static_cast<uint8_t>(port >> 8));
  msg.push_back(static_cast<uint8_t>(port & 0xff));
  uint8_t out[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key.data(), key.size(), msg.data(), msg.size(), out);
  return py::hex(out, sizeof(out)).substr(0, 2 * kServerIdSize);
}

}
