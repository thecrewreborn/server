#include <string>
#include <vector>

#include "core/admin.h"
#include "core/config.h"
#include "core/config_edit.h"
#include "core/core.h"
#include "core/env.h"
#include "core/files.h"
#include "core/util.h"

namespace tcr::admin {

namespace {

struct Key {
  const char* section;
  const char* key;
  const char* type;
  const char* fallback;
  bool live;
  std::vector<const char*> choices;
};

const std::vector<Key>& keys() {
  static const std::vector<Key> k = {
      {"server", "ip", "address", "", false, {}},
      {"server", "http_port", "number", "80", false, {}},
      {"server", "https_port", "number", "443", false, {}},
      {"server", "tls_cert", "text", "certs/server.crt", false, {}},
      {"server", "tls_key", "text", "certs/server.key", false, {}},
      {"server", "save_path", "text", "", false, {}},
      {"server", "link_home", "switch", "on", true, {}},
      {"proxy", "key", "hidden", "", false, {}},
      {"proxy", "iv", "hidden", "", false, {}},
      {"relay", "ip", "address", "0.0.0.0", false, {}},
      {"relay", "port", "number", "4000", false, {}},
      {"player", "abilities", "choice", "none", false, {"none", "all"}},
      {"player", "crew_credits", "number", "1000000", true, {}},
      {"player", "crew_credits_refill", "switch", "on", true, {}},
      {"player", "dlc", "switch", "off", false, {}},
      {"world", "multiplayer", "choice", "lan", true, {"lan", "on", "off"}},
      {"world", "bots", "switch", "off", false, {}},
      {"world", "afk_kick", "switch", "off", false, {}},
      {"ui", "enabled", "switch", "on", false, {}},
      {"ui", "open", "switch", "on", false, {}},
      {"ui", "port", "number", "8090", false, {}},
      {"ui", "password", "hidden", "", false, {}},
  };
  return k;
}

const Key* find_key(const std::string& section, const std::string& key) {
  for (const Key& k : keys()) {
    if (section == k.section && key == k.key) return &k;
  }
  return nullptr;
}

std::string config_path(Core& core) {
  return core.env().config_path().value_or(py::native_paths().join(core.home(), "config.ini"));
}

std::string crash_text(const config::Error& e) {
  return (e.configparser() ? "configparser." : "") + e.type + ": " + e.message;
}

template <class T>
bool failed(const config::Result<T>& r, std::string* text) {
  if (r.ok()) return false;
  *text = crash_text(r.error);
  return true;
}

std::string start_refusal(const config::Config& c, std::vector<std::string>* warnings) {
  config::Outcome o = config::check_config(c);
  if (o.kind != config::Outcome::kOk) return o.text;
  std::string t;
  if (failed(c.relay_address(), &t) || failed(c.http_port(), &t) || failed(c.save_path(), &t) ||
      failed(c.save_path_configured(), &t) || failed(c.abilities(), &t) || failed(c.unlocks(), &t) ||
      failed(c.dlc(), &t) || failed(c.https_port(), &t) || failed(c.tls_cert(), &t) || failed(c.advertised_ip(), &t) ||
      failed(c.bots(), &t) || failed(c.afk_kick(), &t) || failed(c.proxy_key(), &t) || failed(c.crew_credits(), &t) ||
      failed(c.crew_credits_refill(), &t) || failed(c.multiplayer(), &t) || failed(c.link_home(), &t)) {
    return t;
  }
  o = config::check_server_ip(c, warnings);
  if (o.kind != config::Outcome::kOk) return o.text;
  config::Result<std::optional<bool>> enabled = c.ui_enabled();
  config::Result<bool> open = c.ui_open();
  config::Result<std::string> port = c.ui_port();
  config::Result<std::string> password = c.ui_password();
  for (const config::Error* e : {enabled.ok() ? nullptr : &enabled.error, open.ok() ? nullptr : &open.error,
                                 port.ok() ? nullptr : &port.error, password.ok() ? nullptr : &password.error}) {
    if (e) return "Admin page: off (config.ini [ui]: " + e->type + ": " + e->message + ")";
  }
  std::optional<int64_t> p = py::int_text_to_i64(*port.value);
  if (!p || *p < 0 || *p > 65535) {
    return "Admin page: off (config.ini [ui] port " + *port.value + " is not a port from 0 to 65535)";
  }
  return std::string();
}

}

json::Value settings(Core& core) {
  json::Value out = json::Value::object();
  const std::string path = config_path(core);
  std::string text, stamp;
  OsError err;
  out.set("path", json::Value::string(path));
  if (!config::read_with_stamp(*core.files(), path, &text, &stamp, &err)) {
    out.set("error", json::Value::string("cannot read " + path + ": " + err.text()));
    return out;
  }
  out.set("stamp", json::Value::string(stamp));
  config::Config c(stamp.empty() ? std::nullopt : std::optional<std::string>(text), path, core.home(),
                   py::native_paths(), core.files());
  if (const config::Error* e = c.read_error()) out.set("error", json::Value::string(crash_text(*e)));
  json::Value list = json::Value::array();
  for (const Key& k : keys()) {
    json::Value e = json::Value::object();
    e.set("section", json::Value::string(k.section));
    e.set("key", json::Value::string(k.key));
    e.set("type", json::Value::string(k.type));
    json::Value choices = json::Value::array();
    for (const char* ch : k.choices) choices.push(json::Value::string(ch));
    e.set("choices", choices);
    e.set("default", json::Value::string(k.fallback));
    e.set("live", json::Value::boolean(k.live));
    bool from_file = !c.read_error() && c.ini().has_option(k.section, k.key);
    std::string value = k.fallback;
    if (from_file) {
      config::Result<std::string> r = c.ini().get(k.section, k.key, k.fallback);
      value = r.ok() ? *r.value : std::string();
    }
    e.set("source", json::Value::string(from_file ? "config.ini" : "default"));
    if (std::string(k.type) == "hidden") {
      e.set("value", json::Value::null());
      e.set("set", json::Value::boolean(!py::strip(value).empty()));
    } else {
      e.set("value", json::Value::string(value));
    }
    list.push(e);
  }
  out.set("settings", list);
  return out;
}

json::Value set_setting(Core& core, const json::Value& body, int* status, std::string* said) {
  auto field = [&](const char* name) -> std::optional<std::string> {
    const json::Value* v = body.get(name);
    if (!v || !v->is_string()) return std::nullopt;
    return v->as_string();
  };
  auto fail = [&](int code, const std::string& text) {
    *status = code;
    json::Value v = json::Value::object();
    v.set("error", json::Value::string(text));
    return v;
  };
  std::optional<std::string> section = field("section"), key = field("key"), value = field("value"),
                             stamp = field("stamp");
  if (!section || !key || !value || !stamp) return fail(400, "section, key, value and stamp must be texts");
  const Key* k = find_key(*section, *key);
  if (!k) return fail(404, "no such setting: [" + *section + "] " + *key);
  if (!config::value_ok(*value) || value->size() > 1024) {
    return fail(400, "the value must be one line of at most 1024 bytes");
  }
  const std::string path = config_path(core);
  std::string text, now_stamp;
  OsError err;
  if (!config::read_with_stamp(*core.files(), path, &text, &now_stamp, &err)) {
    return fail(500, "cannot read " + path + ": " + err.text());
  }
  if (now_stamp != *stamp) {
    json::Value v = fail(409, "config.ini changed since the page read it: reload the settings");
    v.set("changed", json::Value::boolean(true));
    return v;
  }
  std::string candidate = config::set_value(text, *section, *key, *value);
  config::Config c(candidate, path, core.home(), py::native_paths(), core.files());
  std::vector<std::string> warnings;
  std::string refusal = start_refusal(c, &warnings);
  if (!refusal.empty()) return fail(400, refusal);
  std::string new_stamp;
  switch (config::save_value(*core.files(), path, *stamp, *section, *key, *value, &new_stamp, &err)) {
    case config::SaveOutcome::kChanged: {
      json::Value v = fail(409, "config.ini changed since the page read it: reload the settings");
      v.set("changed", json::Value::boolean(true));
      return v;
    }
    case config::SaveOutcome::kFailed:
      return fail(500, "cannot write " + path + ": " + err.text());
    case config::SaveOutcome::kLink:
      return fail(409, path + " is a symbolic link: the page does not replace it. Change the file it points to by "
                        "hand, or set TCREBORN_CONFIG to that file.");
    case config::SaveOutcome::kSaved:
      break;
  }
  bool secret = std::string(k->type) == "hidden";
  *said = "config.ini [" + *section + "] " + *key + (secret ? " changed" : " = " + *value) +
          (k->live ? " (counts at once)" : " (counts after a restart)");
  *status = 200;
  json::Value v = json::Value::object();
  v.set("ok", json::Value::boolean(true));
  v.set("text", json::Value::string(*said));
  v.set("stamp", json::Value::string(new_stamp));
  v.set("live", json::Value::boolean(k->live));
  json::Value w = json::Value::array();
  for (const std::string& line : warnings) w.push(json::Value::string(line));
  v.set("warnings", w);
  return v;
}

}
