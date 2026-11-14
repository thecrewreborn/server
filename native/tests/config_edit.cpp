#include <cstdio>
#include <string>

#include "core/config_edit.h"
#include "platform/files.h"

int main(int argc, char** argv) {
  tcr::platform::SystemFiles files;
  tcr::OsError err;
  std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "set" && argc == 6) {
    std::string text, stamp;
    if (!tcr::config::read_with_stamp(files, argv[2], &text, &stamp, &err)) return 2;
    std::string out = tcr::config::set_value(text, argv[3], argv[4], argv[5]);
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
  }
  if (mode == "stamp" && argc == 3) {
    std::string text, stamp;
    if (!tcr::config::read_with_stamp(files, argv[2], &text, &stamp, &err)) return 2;
    std::printf("%s", stamp.c_str());
    return 0;
  }
  if (mode == "save" && argc == 7) {
    std::string stamp;
    switch (tcr::config::save_value(files, argv[2], argv[3], argv[4], argv[5], argv[6], &stamp, &err)) {
      case tcr::config::SaveOutcome::kSaved:
        std::printf("saved %s", stamp.c_str());
        return 0;
      case tcr::config::SaveOutcome::kChanged:
        std::printf("changed");
        return 0;
      case tcr::config::SaveOutcome::kLink:
        std::printf("link");
        return 0;
      case tcr::config::SaveOutcome::kFailed:
        std::printf("failed %s", err.text().c_str());
        return 0;
    }
  }
  std::fprintf(stderr, "usage: see the header of tests/config_edit.cpp\n");
  return 2;
}
