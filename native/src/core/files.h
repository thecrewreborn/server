#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/util.h"

namespace tcr {

struct OsError {
  int code = 0;
  bool winerror = false;
  std::string message;
  std::optional<std::string> filename;
  std::optional<std::string> filename2;

  std::string raised_type;
  std::string raised_text;

  std::string text() const;
  const char* type() const;
};

struct FileStat {
  uint64_t size = 0;
  uint32_t mode = 0;
  int64_t mtime_ns = 0;
  uint64_t ino = 0;
};

class Files {
 public:
  virtual ~Files() = default;

  virtual bool read(const std::string& path, std::string* data, OsError* err) = 0;
  virtual bool write_atomic(const std::string& path, std::string_view data, bool private_file, int64_t owner_uid,
                            int64_t owner_gid, FileStat* st, OsError* err) = 0;
  virtual bool stat(const std::string& path, FileStat* st, OsError* err) = 0;
  virtual bool read_stat(const std::string& path, std::string* data, FileStat* st, OsError* err) = 0;
  virtual bool makedirs(const std::string& path, OsError* err) = 0;
  virtual bool replace(const std::string& src, const std::string& dst, OsError* err) = 0;
  virtual bool rename(const std::string& src, const std::string& dst, OsError* err) = 0;
  virtual bool exists(const std::string& path) = 0;
  virtual bool lexists(const std::string& path) = 0;
  virtual bool listdir(const std::string& path, std::vector<std::string>* names, OsError* err) = 0;
  virtual bool read_head(const std::string& path, size_t limit, std::string* data, OsError* err);
  virtual bool copyfile(const std::string& src, const std::string& dst, OsError* err);
  virtual bool remove(const std::string& path, OsError* err);

  virtual int text_code_page() { return 0; }
  virtual bool decode_code_page(int code_page, const std::string& data, std::string* out, std::string* error);

  virtual bool abspath(const std::string& path, std::string* out, OsError* err);
  virtual std::string dirname(const std::string& path);
  virtual std::string join(const std::string& a, const std::string& b);
  virtual bool relpath(const std::string& path, const std::string& start, std::string* out, OsError* err);
};

std::string posix_normpath(const std::string& p);
std::string posix_dirname(const std::string& p);
std::string posix_join(const std::string& a, const std::string& b);

bool not_found(const OsError& e);

std::optional<std::string> decode_text_file(Files* files, const std::string& data, bool whole, std::string* text);

}
