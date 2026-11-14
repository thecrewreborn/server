#include "core/accounts.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "core/scrypt.h"
#include "core/util.h"
#include "mbedtls/md.h"

namespace tcr {

std::string save_name(const std::string& name) {
  std::string out;
  size_t count = 0;
  for (uint32_t cp : py::code_points(name)) {
    if (count == 32) break;
    bool keep = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9') || cp == '_' ||
                cp == '-';
    out += keep ? static_cast<char>(cp) : '_';
    count++;
  }
  return out.empty() ? std::string(kDefaultName) : out;
}

std::string account_key(const std::string& name) {
  std::string s = save_name(name);
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

namespace {

bool number_after(const json::Value& v, double now) {
  switch (v.type()) {
    case json::Type::Bool:
      return (v.as_bool() ? 1.0 : 0.0) > now;
    case json::Type::Int:
    case json::Type::BigInt: {
      std::optional<int> c = py::compare_int_float(v.int_text(), now);
      return c && *c > 0;
    }
    case json::Type::Float:
      return v.as_float() > now;
    default:
      return false;
  }
}

json::Value links_of(const json::Value& data) {
  const json::Value* links = data.get(kLinks);
  return links && links->is_object() ? *links : json::Value::object();
}

std::vector<const json::Value*> credentials(const json::Value& entry) {
  std::vector<const json::Value*> out;
  if (!entry.is_object()) return out;
  const json::Value* creds = entry.get("credentials");
  if (!creds || !creds->is_array()) return out;
  for (const json::Value& c : creds->items()) {
    if (c.is_object()) out.push_back(&c);
  }
  return out;
}

json::Value normalized(const json::Value& entry) {
  if (!entry.is_object() || entry.contains("credentials")) return entry;
  const json::Value* salt = entry.get("salt");
  if (!salt || !salt->is_string()) return entry;
  json::Value out = json::Value::object();
  for (const json::Member& m : entry.members()) {
    if (m.key != "salt" && m.key != "hash") out.set(m.key, m.value);
  }
  out.set("id", *salt);
  json::Value cred = json::Value::object();
  cred.set("salt", *salt);
  const json::Value* hash = entry.get("hash");
  cred.set("hash", hash ? *hash : json::Value());
  const json::Value* platform = entry.get("platform");
  cred.set("platform", platform ? *platform : json::Value::string("?"));
  const json::Value* created = entry.get("created");
  cred.set("added", created ? *created : json::Value::integer(0));
  json::Value list = json::Value::array();
  list.push(cred);
  out.set("credentials", list);
  return out;
}

json::Value int_value(const std::string& text) { return json::Value::integer_text(text); }

const json::Value* entry_of(const json::Value& data, const std::string& key) {
  const json::Value* v = data.get(key);
  return v && !v->is_null() ? v : nullptr;
}

}

AccountStore::AccountStore(Env& env, Files& files, std::string path, std::array<uint8_t, 32> process_key,
                           int64_t owner_uid, int64_t owner_gid)
    : env_(env),
      files_(files),
      path_(std::move(path)),
      process_key_(process_key),
      owner_uid_(owner_uid),
      owner_gid_(owner_gid) {}

std::optional<Raised> utf8_encode_error(const std::string& text) {
  std::vector<uint32_t> cps = py::code_points(text);
  for (size_t i = 0; i < cps.size(); i++) {
    if (cps[i] < 0xD800 || cps[i] > 0xDFFF) continue;
    size_t end = i + 1;
    while (end < cps.size() && cps[end] >= 0xD800 && cps[end] <= 0xDFFF) end++;
    char buf[96];
    if (end - i == 1) {
      std::snprintf(buf, sizeof(buf), "'utf-8' codec can't encode character '\\u%04x' in position %zu: ", cps[i], i);
    } else {
      std::snprintf(buf, sizeof(buf), "'utf-8' codec can't encode characters in position %zu-%zu: ", i, end - 1);
    }
    return Raised{"UnicodeEncodeError", std::string(buf) + "surrogates not allowed"};
  }
  return std::nullopt;
}

bool AccountStore::read(json::Value* data, std::optional<std::pair<int64_t, uint64_t>>* sig, Err& err) {
  std::string raw;
  FileStat st;
  OsError oserr;
  if (!files_.read_stat(path_, &raw, &st, &oserr)) {
    if (not_found(oserr)) {
      *data = json::Value::object();
      sig->reset();
      return true;
    }
    env_.print_line("could not read " + path_ + ": " + oserr.text());
    return false;
  }
  json::Failure why = json::Failure::kSyntax;
  std::optional<json::Value> parsed = json::loads_utf8(raw, json::kHttpMaxDepth, &why);
  if (!parsed && why != json::Failure::kSyntax) {
    err = Raised{"RecursionError", json::recursion_text(why)};
    return false;
  }
  if (!parsed || !parsed->is_object()) {
    std::string aside = path_ + ".corrupt-" + py::float_to_int_text(env_.time()).value_or("0");
    OsError moved;
    if (!files_.replace(path_, aside, &moved)) return false;
    env_.print_line(path_ + " is corrupt: moved to " + aside + ", starting empty");
    *data = json::Value::object();
    sig->reset();
    return true;
  }
  json::Value out = json::Value::object();
  for (const json::Member& m : parsed->members()) out.set(m.key, m.key == kLinks ? m.value : normalized(m.value));
  *data = std::move(out);
  *sig = std::make_pair(st.mtime_ns, st.size);
  return true;
}

const json::Value* AccountStore::load(Err& err) {
  FileStat st;
  OsError oserr;
  std::optional<std::pair<int64_t, uint64_t>> sig;
  if (files_.stat(path_, &st, &oserr)) {
    sig = std::make_pair(st.mtime_ns, st.size);
  } else if (!not_found(oserr)) {
    return nullptr;
  }
  if (!have_data_ || sig != sig_) {
    json::Value data;
    std::optional<std::pair<int64_t, uint64_t>> read_sig;
    if (!read(&data, &read_sig, err)) return nullptr;
    data_ = std::move(data);
    sig_ = read_sig;
    have_data_ = true;
  }
  return &data_;
}

bool AccountStore::save(const json::Value& data) {
  double now = env_.time();
  json::Value links = json::Value::object();
  const json::Value stored = links_of(data);
  for (const json::Member& m : stored.members()) {
    const json::Value* owner = data.get(m.key);
    if (number_after(m.value, now) && owner && owner->is_object()) links.set(m.key, m.value);
  }
  json::Value out = json::Value::object();
  for (const json::Member& m : data.members()) {
    if (m.key != kLinks) out.set(m.key, m.value);
  }
  if (!links.members().empty()) out.set(kLinks, links);
  FileStat st;
  OsError err;
  if (!files_.write_atomic(path_, json::dumps(out, 1), true, owner_uid_, owner_gid_, &st, &err)) {
    env_.print_line("could not write " + path_ + ": " + err.text());
    return false;
  }
  data_ = std::move(out);
  sig_ = std::make_pair(st.mtime_ns, st.size);
  have_data_ = true;
  return true;
}

namespace {

struct HashJob {
  const std::string* secret;
  const Bytes* salt;
  std::optional<std::string> out;
};

void run_hash(void* p) {
  HashJob* job = static_cast<HashJob*>(p);
  job->out = account_hash(*job->secret, *job->salt);
}

}

std::optional<std::string> AccountStore::hash(const std::string& secret, const Bytes& salt, Err& err) {
  HashJob job{&secret, &salt, std::nullopt};
  env_.hash_unlocked(run_hash, &job);
  if (!job.out) err = Raised{"MemoryError", ""};
  return job.out;
}

std::optional<std::string> AccountStore::mark(const std::string& key, const std::string& salt,
                                              const std::string& secret, Err& err) const {
  std::string msg = key + std::string(1, '\0') + salt + std::string(1, '\0') + secret;
  if (std::optional<Raised> e = utf8_encode_error(msg)) {
    err = e;
    return std::nullopt;
  }
  uint8_t out[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), process_key_.data(), process_key_.size(),
                  reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), out);
  return std::string(reinterpret_cast<const char*>(out), sizeof(out));
}

void AccountStore::remember(const std::string& m, double now) {
  verified_.erase(m);
  verified_.set(m, now);
  while (!verified_.empty() && (verified_.size() > kVerifiedMax || now - verified_.front().second >= kVerifiedTtl)) {
    verified_.pop_front();
  }
}

std::string AccountStore::verify(const std::string& key, const json::Value& entry, const std::string& secret,
                                 Err& err) {
  struct Try {
    const json::Value* cred;
    std::string salt;
    std::string mark;
  };
  std::vector<Try> tries;
  for (const json::Value* cred : credentials(entry)) {
    const json::Value* salt = cred->get("salt");
    if (!salt || !salt->is_string()) continue;
    std::optional<std::string> m = mark(key, salt->as_string(), secret, err);
    if (!m) return "unavailable";
    tries.push_back({cred, salt->as_string(), *m});
  }
  double now = env_.monotonic();
  for (const Try& t : tries) {
    const double* when = verified_.get(t.mark);
    if (when && now - *when < kVerifiedTtl) return "ok";
  }
  const Try* matched = nullptr;
  for (const Try& t : tries) {
    std::optional<Bytes> salt = py::fromhex(t.salt);
    if (!salt) continue;
    std::optional<std::string> hashed = hash(secret, *salt, err);
    if (!hashed) return "unavailable";
    const json::Value* stored = t.cred->get("hash");
    if (!stored || !stored->is_string()) continue;
    if (stored->as_string() == *hashed) {
      matched = &t;
      break;
    }
  }
  if (!matched) return "wrong";
  remember(matched->mark, now);
  return "ok";
}

bool AccountStore::linked_meanwhile(const std::string& key, const json::Value& entry, const json::Value& current,
                                    const std::string& secret, Err& err) {
  std::set<std::string> known;
  for (const json::Value* cred : credentials(entry)) {
    const json::Value* salt = cred->get("salt");
    if (salt && (salt->is_array() || salt->is_object())) {
      err = Raised{"TypeError", std::string("unhashable type: '") + (salt->is_array() ? "list" : "dict") + "'"};
      return false;
    }
    if (salt && salt->is_string()) known.insert(salt->as_string());
  }
  double now = env_.monotonic();
  for (const json::Value* cred : credentials(current)) {
    const json::Value* salt = cred->get("salt");
    if (salt && salt->is_string() && !known.count(salt->as_string())) {
      std::optional<std::string> m = mark(key, salt->as_string(), secret, err);
      if (!m) return false;
      const double* when = verified_.get(*m);
      if (when && now - *when < kVerifiedTtl) return true;
    }
  }
  return false;
}

bool AccountStore::window_open(const json::Value& data, const std::string& key) {
  json::Value links = links_of(data);
  const json::Value* until = links.get(key);
  return until && number_after(*until, env_.time());
}

std::optional<std::string> AccountStore::may_link(const std::string& key, const json::Value& entry,
                                                  const json::Value& data, const std::string& secret,
                                                  const std::string& link, Err& err) {
  const json::Value* current = data.get(key);
  const json::Value* current_id = current && current->is_object() ? current->get("id") : nullptr;
  json::Value none;
  if (!current || !current->is_object() || (current_id ? *current_id : none) != *entry.get("id")) {
    return std::string("wrong");
  }
  bool meanwhile = linked_meanwhile(key, entry, *current, secret, err);
  if (err) return std::string("unavailable");
  if (meanwhile) return std::string("ok");
  if (link == "window" && !window_open(data, key)) return std::string("wrong");
  if (credentials(*current).size() >= kDevicesMax) return std::string("full");
  return std::nullopt;
}

std::string AccountStore::link(const std::string& key, const json::Value& entry, const std::string& platform,
                               const std::string& secret, const std::string& link, Err& err) {
  const json::Value* id = entry.is_object() ? entry.get("id") : nullptr;
  if ((link != "always" && link != "window") || !id || !id->is_string()) return "wrong";
  const json::Value* data = load(err);
  if (!data) return "unavailable";
  std::optional<std::string> verdict = may_link(key, entry, *data, secret, link, err);
  if (verdict) return *verdict;
  uint8_t raw[16];
  env_.urandom(raw, sizeof(raw));
  std::string salt = py::hex(raw, sizeof(raw));
  std::optional<std::string> hashed = hash(secret, Bytes(raw, raw + sizeof(raw)), err);
  if (!hashed) return "unavailable";
  data = load(err);
  if (!data) return "unavailable";
  verdict = may_link(key, entry, *data, secret, link, err);
  if (verdict) return *verdict;
  const json::Value& current = *data->get(key);
  json::Value creds = json::Value::array();
  for (const json::Value* c : credentials(current)) creds.push(*c);
  json::Value cred = json::Value::object();
  cred.set("salt", json::Value::string(salt));
  cred.set("hash", json::Value::string(*hashed));
  cred.set("platform", json::Value::string(platform));
  cred.set("added", int_value(py::float_to_int_text(env_.time()).value_or("0")));
  creds.push(cred);
  json::Value changed = current;
  changed.set("credentials", creds);
  json::Value links = json::Value::object();
  const json::Value stored = links_of(*data);
  for (const json::Member& m : stored.members()) {
    if (m.key != key) links.set(m.key, m.value);
  }
  json::Value next = *data;
  next.set(key, changed);
  next.set(kLinks, links);
  if (!save(next)) return "unavailable";
  std::optional<std::string> m = mark(key, salt, secret, err);
  if (!m) return "unavailable";
  remember(*m, env_.monotonic());
  return "linked";
}

namespace {

template <class T>
T pass(const std::optional<Raised>& err, Raised* raised, T value) {
  if (err && raised) *raised = *err;
  return value;
}

}

std::string AccountStore::check(const std::string& name, const std::string& platform, const std::string& secret,
                                bool claim, const std::string& link_mode, Raised* raised) {
  Err err;
  if (py::strip(secret).empty()) return "missing";
  std::string key = account_key(name);
  const json::Value* data = load(err);
  if (!data) return pass<std::string>(err, raised, "unavailable");
  const json::Value* found = entry_of(*data, key);
  json::Value entry = found ? *found : json::Value();
  if (!found) {
    if (!claim) return "unclaimed";
    uint8_t salt[16];
    env_.urandom(salt, sizeof(salt));
    std::optional<std::string> hashed = hash(secret, Bytes(salt, salt + sizeof(salt)), err);
    if (!hashed) return pass<std::string>(err, raised, "unavailable");
    data = load(err);
    if (!data) return pass<std::string>(err, raised, "unavailable");
    found = entry_of(*data, key);
    if (!found) {
      json::Value next = *data;
      std::string now = py::float_to_int_text(env_.time()).value_or("0");
      uint8_t id[16];
      json::Value claimed = json::Value::object();
      claimed.set("name", json::Value::string(name));
      claimed.set("platform", json::Value::string(platform));
      claimed.set("created", int_value(now));
      env_.urandom(id, sizeof(id));
      claimed.set("id", json::Value::string(py::hex(id, sizeof(id))));
      json::Value cred = json::Value::object();
      cred.set("salt", json::Value::string(py::hex(salt, sizeof(salt))));
      cred.set("hash", json::Value::string(*hashed));
      cred.set("platform", json::Value::string(platform));
      cred.set("added", int_value(now));
      json::Value creds = json::Value::array();
      creds.push(cred);
      claimed.set("credentials", creds);
      next.set(key, claimed);
      return save(next) ? "claimed" : "unavailable";
    }
    entry = *found;
  }
  std::string verdict = verify(key, entry, secret, err);
  if (err) return pass<std::string>(err, raised, "unavailable");
  if (verdict == "ok") return verdict;
  verdict = this->link(key, entry, platform, secret, link_mode, err);
  return pass<std::string>(err, raised, err ? std::string("unavailable") : verdict);
}

std::optional<bool> AccountStore::open_link(const std::string& name, double minutes, Raised* raised) {
  Err err;
  std::string key = account_key(name);
  const json::Value* data = load(err);
  if (!data && err) return pass<std::optional<bool>>(err, raised, std::nullopt);
  if (!data) return false;
  const json::Value* entry = data->get(key);
  if (!entry || !entry->is_object() || minutes <= 0) return false;
  std::string type, text;
  std::optional<std::string> until = py::float_to_int_text(env_.time() + minutes * 60, &type, &text);
  if (!until) {
    if (raised) *raised = {type, text};
    return std::nullopt;
  }
  json::Value links = links_of(*data);
  links.set(key, int_value(*until));
  json::Value next = *data;
  next.set(kLinks, links);
  return save(next);
}

bool AccountStore::free(const std::string& name, Raised* raised) {
  Err err;
  std::string key = account_key(name);
  const json::Value* data = load(err);
  if (!data || !data->contains(key)) return pass(err, raised, false);
  json::Value next = json::Value::object();
  for (const json::Member& m : data->members()) {
    if (m.key != key) next.set(m.key, m.value);
  }
  return save(next);
}

std::optional<std::string> AccountStore::display_name(const std::string& name, Raised* raised) {
  Err err;
  const json::Value* data = load(err);
  if (!data) return pass<std::optional<std::string>>(err, raised, std::nullopt);
  const json::Value* entry = data->get(account_key(name));
  const json::Value* stored = entry && entry->is_object() ? entry->get("name") : nullptr;
  if (stored && stored->is_string() && !stored->as_string().empty()) return stored->as_string();
  return std::nullopt;
}

std::optional<std::string> AccountStore::account_id(const std::string& name, Raised* raised) {
  Err err;
  const json::Value* data = load(err);
  if (!data) return pass<std::optional<std::string>>(err, raised, std::nullopt);
  const json::Value* entry = data->get(account_key(name));
  const json::Value* id = entry && entry->is_object() ? entry->get("id") : nullptr;
  if (id && id->is_string() && !id->as_string().empty()) return id->as_string();
  return std::nullopt;
}

json::Value AccountStore::entries(Raised* raised) {
  Err err;
  json::Value out = json::Value::object();
  const json::Value* data = load(err);
  if (!data) return pass(err, raised, out);
  std::vector<const json::Member*> sorted;
  for (const json::Member& m : data->members()) {
    if (m.key != kLinks && m.value.is_object()) sorted.push_back(&m);
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const json::Member* a, const json::Member* b) { return a->key < b->key; });
  json::Value links = links_of(*data);
  for (const json::Member* m : sorted) {
    const json::Value& v = m->value;
    json::Value e = json::Value::object();
    const json::Value* name = v.get("name");
    e.set("name", name ? *name : json::Value::string(m->key));
    const json::Value* platform = v.get("platform");
    e.set("platform", platform ? *platform : json::Value::string("?"));
    const json::Value* created = v.get("created");
    e.set("created", created ? *created : json::Value::integer(0));
    e.set("devices", json::Value::integer(static_cast<int64_t>(credentials(v).size())));
    e.set("link", window_open(*data, m->key) ? *links.get(m->key) : json::Value());
    out.set(m->key, e);
  }
  return out;
}

}
