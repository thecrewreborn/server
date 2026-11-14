#pragma once

#include "core/files.h"

namespace tcr {
namespace platform {

class SystemFiles final : public Files {
 public:
  bool read(const std::string& path, std::string* data, OsError* err) override;
  bool read_head(const std::string& path, size_t limit, std::string* data, OsError* err) override;
  bool write_atomic(const std::string& path, std::string_view data, bool private_file, int64_t owner_uid,
                    int64_t owner_gid, FileStat* st, OsError* err) override;
  bool stat(const std::string& path, FileStat* st, OsError* err) override;
  bool read_stat(const std::string& path, std::string* data, FileStat* st, OsError* err) override;
  bool makedirs(const std::string& path, OsError* err) override;
  bool replace(const std::string& src, const std::string& dst, OsError* err) override;
  bool rename(const std::string& src, const std::string& dst, OsError* err) override;
  bool exists(const std::string& path) override;
  bool lexists(const std::string& path) override;
  bool listdir(const std::string& path, std::vector<std::string>* names, OsError* err) override;
  bool remove(const std::string& path, OsError* err) override;
  bool copyfile(const std::string& src, const std::string& dst, OsError* err) override;
  bool abspath(const std::string& path, std::string* out, OsError* err) override;
#if defined(_WIN32)
  int text_code_page() override;
  bool decode_code_page(int code_page, const std::string& data, std::string* out, std::string* error) override;
  std::string dirname(const std::string& path) override;
  std::string join(const std::string& a, const std::string& b) override;
  bool relpath(const std::string& path, const std::string& start, std::string* out, OsError* err) override;
#endif
};

}
}
