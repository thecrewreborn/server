#include "core/files.h"

#include <cerrno>
#include <cstddef>
#include <string>
#include <vector>

namespace tcr {

bool not_found(const OsError& e) { return std::string(e.type()) == "FileNotFoundError"; }

const char* OsError::type() const {
  if (!raised_type.empty()) return raised_type.c_str();
  if (winerror) {
    switch (code) {
      case 2: case 3: case 15: case 18: case 53: case 67: case 161: case 206:
        return "FileNotFoundError";
      case 5: case 16: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28:
      case 29: case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 65: case 82: case 83: case 108:
      case 132: case 158: case 167:
        return "PermissionError";
      case 80: case 183:
        return "FileExistsError";
      case 89: case 164: case 215:
        return "BlockingIOError";
      case 109: case 232:
        return "BrokenPipeError";
      case 128: case 129:
        return "ChildProcessError";
      case 267:
        return "NotADirectoryError";
      default:
        return "OSError";
    }
  }
  switch (code) {
    case ENOENT: return "FileNotFoundError";
    case EACCES: case EPERM:
#ifdef ENOTCAPABLE
    case ENOTCAPABLE:
#endif
      return "PermissionError";
    case EEXIST: return "FileExistsError";
    case ENOTDIR: return "NotADirectoryError";
    case EISDIR: return "IsADirectoryError";
    case EINTR: return "InterruptedError";
    case EAGAIN: case EALREADY: case EINPROGRESS:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
      return "BlockingIOError";
    case EPIPE:
#ifdef ESHUTDOWN
    case ESHUTDOWN:
#endif
      return "BrokenPipeError";
    case ECHILD: return "ChildProcessError";
    case ECONNABORTED: return "ConnectionAbortedError";
    case ECONNREFUSED: return "ConnectionRefusedError";
    case ECONNRESET: return "ConnectionResetError";
    case ESRCH: return "ProcessLookupError";
    case ETIMEDOUT: return "TimeoutError";
    default: return "OSError";
  }
}

std::string OsError::text() const {
  if (!raised_type.empty()) return raised_text;
  std::string out = (winerror ? "[WinError " : "[Errno ") + std::to_string(code) + "] " + message;
  if (filename) {
    out += ": " + py::repr(*filename);
    if (filename2) out += " -> " + py::repr(*filename2);
  }
  return out;
}

std::string posix_normpath(const std::string& p) {
  if (p.empty()) return ".";
  size_t lead = 0;
  while (lead < p.size() && p[lead] == '/') lead++;
  std::string prefix = lead == 0 ? "" : lead == 2 ? "//" : "/";
  std::vector<std::string> parts;
  size_t i = 0;
  while (i <= p.size()) {
    size_t j = p.find('/', i);
    if (j == std::string::npos) j = p.size();
    std::string part = p.substr(i, j - i);
    if (part.empty() || part == ".") {
    } else if (part != ".." || (prefix.empty() && parts.empty()) || (!parts.empty() && parts.back() == "..")) {
      parts.push_back(part);
    } else if (!parts.empty()) {
      parts.pop_back();
    }
    i = j + 1;
  }
  std::string out = prefix;
  for (size_t k = 0; k < parts.size(); k++) out += (k ? "/" : "") + parts[k];
  return out.empty() ? "." : out;
}

std::string posix_dirname(const std::string& p) {
  size_t k = p.rfind('/');
  std::string head = k == std::string::npos ? std::string() : p.substr(0, k + 1);
  if (!head.empty() && head.find_first_not_of('/') != std::string::npos) {
    while (!head.empty() && head.back() == '/') head.pop_back();
  }
  return head;
}

std::string posix_join(const std::string& a, const std::string& b) {
  if (!b.empty() && b[0] == '/') return b;
  if (a.empty() || a.back() == '/') return a + b;
  return a + "/" + b;
}

bool Files::abspath(const std::string& path, std::string* out, OsError* err) {
  if (!path.empty() && path[0] == '/') {
    *out = posix_normpath(path);
    return true;
  }
  err->code = ENOENT;
  err->message = "No such file or directory";
  return false;
}

bool Files::decode_code_page(int code_page, const std::string& data, std::string* out, std::string* error) {
  (void)data;
  out->clear();
  *error = "no decoder for code page " + std::to_string(code_page);
  return false;
}

std::optional<std::string> decode_text_file(Files* files, const std::string& data, bool whole, std::string* text) {
  const int code_page = files ? files->text_code_page() : 0;
  if (!py::has_text_codec(code_page)) {
    std::string error;
    if (files->decode_code_page(code_page, data, text, &error)) return std::nullopt;
    return error;
  }
  return py::text_file_decode_error(reinterpret_cast<const uint8_t*>(data.data()), data.size(), text, code_page,
                                    whole);
}

bool Files::read_head(const std::string& path, size_t limit, std::string* data, OsError* err) {
  if (!read(path, data, err)) return false;
  if (data->size() > limit) data->resize(limit);
  return true;
}

bool Files::copyfile(const std::string& src, const std::string& dst, OsError* err) {
  std::string data;
  if (!read(src, &data, err)) return false;
  FileStat st;
  return write_atomic(dst, data, false, -1, -1, &st, err);
}

bool Files::remove(const std::string& path, OsError* err) {
  err->code = EPERM;
  err->message = "Operation not permitted";
  err->filename = path;
  return false;
}

std::string Files::dirname(const std::string& path) { return posix_dirname(path); }

std::string Files::join(const std::string& a, const std::string& b) { return posix_join(a, b); }

bool Files::relpath(const std::string& path, const std::string& start, std::string* out, OsError* err) {
  if (path.empty()) {
    err->code = 0;
    err->message = "no path specified";
    return false;
  }
  std::string a, b;
  if (!abspath(start, &a, err) || !abspath(path, &b, err)) return false;
  auto split = [](const std::string& s) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= s.size()) {
      size_t j = s.find('/', i);
      if (j == std::string::npos) j = s.size();
      if (j > i) parts.push_back(s.substr(i, j - i));
      i = j + 1;
    }
    return parts;
  };
  std::vector<std::string> start_list = split(a), path_list = split(b);
  size_t i = 0;
  while (i < start_list.size() && i < path_list.size() && start_list[i] == path_list[i]) i++;
  std::vector<std::string> rel(start_list.size() - i, "..");
  rel.insert(rel.end(), path_list.begin() + static_cast<std::ptrdiff_t>(i), path_list.end());
  if (rel.empty()) {
    *out = ".";
    return true;
  }
  std::string r;
  for (size_t k = 0; k < rel.size(); k++) r += (k ? "/" : "") + rel[k];
  *out = r;
  return true;
}

}
