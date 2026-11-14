#pragma once

#include <string>

#include "core/files.h"

namespace tcr {
namespace config {

bool value_ok(const std::string& value);

std::string set_value(const std::string& text, const std::string& section, const std::string& key,
                      const std::string& value);

std::string stamp_of(const FileStat& st);

bool read_with_stamp(Files& files, const std::string& path, std::string* text, std::string* stamp, OsError* err);

enum class SaveOutcome { kSaved, kChanged, kFailed, kLink };

SaveOutcome save_value(Files& files, const std::string& path, const std::string& expected_stamp,
                       const std::string& section, const std::string& key, const std::string& value,
                       std::string* new_stamp, OsError* err);

}
}
