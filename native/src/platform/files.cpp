#include "platform/files.h"

#include "core/util.h"
#include "platform/platform.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <tuple>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tcr {
namespace platform {

namespace {

OsError os_error(int code, std::optional<std::string> filename = std::nullopt,
                 std::optional<std::string> filename2 = std::nullopt) {
  OsError e;
  e.code = code;
  {
    static std::mutex lock;
    std::lock_guard<std::mutex> held(lock);
    e.message = std::strerror(code);
  }
  e.filename = std::move(filename);
  e.filename2 = std::move(filename2);
  return e;
}

#if defined(_WIN32)

std::wstring wide(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

OsError win_error(DWORD code, std::optional<std::string> filename, std::optional<std::string> filename2) {
  OsError e;
  e.code = static_cast<int>(code);
  e.winerror = true;
  wchar_t* buf = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                 code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
  if (buf) {
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, buf, -1, &text[0], n, nullptr, nullptr);
    while (!text.empty() && (static_cast<unsigned char>(text.back()) <= ' ' || text.back() == '.')) text.pop_back();
    e.message = text;
    LocalFree(buf);
  }
  e.filename = std::move(filename);
  e.filename2 = std::move(filename2);
  return e;
}

bool handle_stat(HANDLE h, FileStat* st) {
  BY_HANDLE_FILE_INFORMATION info;
  if (h == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(h, &info)) return false;
  uint64_t ft = (static_cast<uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
  st->mtime_ns = (static_cast<int64_t>(ft) - 116444736000000000LL) * 100;
  st->size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
  st->ino = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
  return true;
}

bool fd_stat(int fd, FileStat* st) { return handle_stat(reinterpret_cast<HANDLE>(_get_osfhandle(fd)), st); }

bool is_dir(const std::string& p) {
  DWORD a = GetFileAttributesW(wide(p).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool exists(const std::string& p) { return GetFileAttributesW(wide(p).c_str()) != INVALID_FILE_ATTRIBUTES; }

bool makedirs(const std::string& name, OsError* err) {
  const py::NtPaths nt;
  auto [head, tail] = nt.split(name);
  if (tail.empty()) std::tie(head, tail) = nt.split(head);
  if (!head.empty() && !tail.empty() && !exists(head)) {
    OsError inner;
    if (!makedirs(head, &inner) && inner.type() != std::string("FileExistsError")) {
      *err = inner;
      return false;
    }
    if (tail == ".") return true;
  }
  if (!CreateDirectoryW(wide(name).c_str(), nullptr)) {
    DWORD code = GetLastError();
    if (is_dir(name)) return true;
    *err = win_error(code, name, std::nullopt);
    return false;
  }
  return true;
}

bool absolute(const std::string& p, std::string* result, OsError* err) {
  std::wstring w = wide(p);
  DWORD n = GetFullPathNameW(w.c_str(), 0, nullptr, nullptr);
  std::wstring out(n, L'\0');
  if (n) n = GetFullPathNameW(w.c_str(), n, &out[0], nullptr);
  if (!n) {
    *err = win_error(GetLastError(), p, std::nullopt);
    return false;
  }
  out.resize(n);
  int m = WideCharToMultiByte(CP_UTF8, 0, out.data(), static_cast<int>(out.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(m), '\0');
  WideCharToMultiByte(CP_UTF8, 0, out.data(), static_cast<int>(out.size()), &s[0], m, nullptr, nullptr);
  *result = s;
  return true;
}

#else

bool is_dir(const std::string& p) {
  struct stat s;
  return ::stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode);
}

bool exists(const std::string& p) {
  struct stat s;
  return ::stat(p.c_str(), &s) == 0;
}

std::string normpath(const std::string& p) {
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

bool absolute(const std::string& p, std::string* out, OsError* err) {
  if (!p.empty() && p[0] == '/') {
    *out = normpath(p);
    return true;
  }
  std::vector<char> buf(4096);
  while (!getcwd(buf.data(), buf.size())) {
    if (errno != ERANGE) {
      *err = os_error(errno);
      return false;
    }
    buf.resize(buf.size() * 2);
  }
  std::string cwd(buf.data());
  *out = normpath(cwd + (cwd.size() && cwd.back() == '/' ? "" : "/") + p);
  return true;
}

bool makedirs(const std::string& name, OsError* err) {
  const py::PosixPaths posix;
  auto [head, tail] = posix.split(name);
  if (tail.empty()) std::tie(head, tail) = posix.split(head);
  if (!head.empty() && !tail.empty() && !exists(head)) {
    OsError inner;
    if (!makedirs(head, &inner) && inner.code != EEXIST) {
      *err = inner;
      return false;
    }
    if (tail == ".") return true;
  }
  if (::mkdir(name.c_str(), 0777) != 0) {
    int code = errno;
    if (is_dir(name)) return true;
    *err = os_error(code, name);
    return false;
  }
  return true;
}

#endif

std::string dirname_of(const std::string& path) { return py::native_paths().split(path).first; }

}

namespace {

void fill_stat(const
#if defined(_WIN32)
               struct _stat64& s,
#else
               struct stat& s,
#endif
               FileStat* st) {
  st->size = static_cast<uint64_t>(s.st_size);
  st->mode = static_cast<uint32_t>(s.st_mode);
#if defined(_WIN32)
  st->mtime_ns = static_cast<int64_t>(s.st_mtime) * 1000000000;
  st->ino = 0;
#else
#if defined(__APPLE__)
  st->mtime_ns = static_cast<int64_t>(s.st_mtimespec.tv_sec) * 1000000000 + s.st_mtimespec.tv_nsec;
#else
  st->mtime_ns = static_cast<int64_t>(s.st_mtim.tv_sec) * 1000000000 + s.st_mtim.tv_nsec;
#endif
  st->ino = static_cast<uint64_t>(s.st_ino);
#endif
}

bool read_file(const std::string& path, std::string* data, FileStat* st, OsError* err,
               size_t limit = static_cast<size_t>(-1)) {
#if defined(_WIN32)
  int fd = _wopen(wide(path).c_str(), _O_RDONLY | _O_BINARY);
#else
  int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
#endif
  if (fd < 0) {
    *err = os_error(errno, path);
    return false;
  }
#if defined(_WIN32)
  struct _stat64 s;
  if (_fstat64(fd, &s) != 0) {
    int code = errno;
    _close(fd);
    *err = os_error(code);
    return false;
  }
#else
  struct stat s;
  if (fstat(fd, &s) != 0) {
    int code = errno;
    ::close(fd);
    *err = os_error(code);
    return false;
  }
  if (S_ISDIR(s.st_mode)) {
    ::close(fd);
    *err = os_error(EISDIR, path);
    return false;
  }
#endif
  if (st) {
    fill_stat(s, st);
#if defined(_WIN32)
    fd_stat(fd, st);
#endif
  }
  data->clear();
  char buf[65536];
  while (data->size() < limit) {
    const size_t want = std::min(sizeof(buf), limit - data->size());
#if defined(_WIN32)
    int n = _read(fd, buf, static_cast<unsigned>(want));
#else
    ssize_t n = ::read(fd, buf, want);
#endif
    if (n < 0) {
      if (errno == EINTR) continue;
      int code = errno;
#if defined(_WIN32)
      _close(fd);
#else
      ::close(fd);
#endif
      *err = os_error(code);
      return false;
    }
    if (n == 0) break;
    data->append(buf, static_cast<size_t>(n));
  }
#if defined(_WIN32)
  _close(fd);
#else
  ::close(fd);
#endif
  return true;
}

}

bool SystemFiles::read(const std::string& path, std::string* data, OsError* err) {
  return read_file(path, data, nullptr, err);
}

bool SystemFiles::read_head(const std::string& path, size_t limit, std::string* data, OsError* err) {
  return read_file(path, data, nullptr, err, limit);
}

bool SystemFiles::read_stat(const std::string& path, std::string* data, FileStat* st, OsError* err) {
  return read_file(path, data, st, err);
}

bool SystemFiles::makedirs(const std::string& path, OsError* err) { return platform::makedirs(path, err); }

bool SystemFiles::replace(const std::string& src, const std::string& dst, OsError* err) {
#if defined(_WIN32)
  if (!MoveFileExW(wide(src).c_str(), wide(dst).c_str(), MOVEFILE_REPLACE_EXISTING)) {
    *err = win_error(GetLastError(), src, dst);
    return false;
  }
  return true;
#else
  if (::rename(src.c_str(), dst.c_str()) != 0) {
    *err = os_error(errno, src, dst);
    return false;
  }
  return true;
#endif
}

bool SystemFiles::rename(const std::string& src, const std::string& dst, OsError* err) {
#if defined(_WIN32)
  if (!MoveFileExW(wide(src).c_str(), wide(dst).c_str(), 0)) {
    *err = win_error(GetLastError(), src, dst);
    return false;
  }
  return true;
#else
  return replace(src, dst, err);
#endif
}

namespace {

bool stat_identity(const std::string& path, uint64_t* dev, uint64_t* ino, bool* fifo) {
#if defined(_WIN32)
  HANDLE h = CreateFileW(wide(path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  BY_HANDLE_FILE_INFORMATION info;
  bool ok = GetFileInformationByHandle(h, &info) != 0;
  CloseHandle(h);
  if (!ok) return false;
  *dev = info.dwVolumeSerialNumber;
  *ino = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
  *fifo = false;
  return true;
#else
  struct stat s;
  if (::stat(path.c_str(), &s) != 0) return false;
  *dev = static_cast<uint64_t>(s.st_dev);
  *ino = static_cast<uint64_t>(s.st_ino);
  *fifo = S_ISFIFO(s.st_mode);
  return true;
#endif
}

}

bool SystemFiles::copyfile(const std::string& src, const std::string& dst, OsError* err) {
  uint64_t dev_a = 0, ino_a = 0, dev_b = 0, ino_b = 0;
  bool fifo_a = false, fifo_b = false;
  bool have_a = stat_identity(src, &dev_a, &ino_a, &fifo_a);
  bool have_b = stat_identity(dst, &dev_b, &ino_b, &fifo_b);
  if (have_a && have_b && dev_a == dev_b && ino_a == ino_b) {
    err->raised_type = "shutil.SameFileError";
    err->raised_text = py::repr(src) + " and " + py::repr(dst) + " are the same file";
    return false;
  }
  const std::string* pipe = have_a && fifo_a ? &src : have_b && fifo_b ? &dst : nullptr;
  if (pipe) {
    err->raised_type = "shutil.SpecialFileError";
    err->raised_text = "`" + *pipe + "` is a named pipe";
    return false;
  }
  std::string data;
  if (!read(src, &data, err)) return false;
#if defined(_WIN32)
  int fd = _wopen(wide(dst).c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
  int fd = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
#endif
  if (fd < 0) {
    int code = errno;
    if (code == EISDIR && !exists(dst)) {
      err->raised_type = "FileNotFoundError";
      err->raised_text = "Directory does not exist: " + dst;
      return false;
    }
    *err = os_error(code, dst);
    return false;
  }
  size_t done = 0;
  while (done < data.size()) {
#if defined(_WIN32)
    int n = _write(fd, data.data() + done, static_cast<unsigned>(std::min<size_t>(data.size() - done, 1u << 30)));
#else
    ssize_t n = ::write(fd, data.data() + done, data.size() - done);
#endif
    if (n < 0) {
      if (errno == EINTR) continue;
      int code = errno;
#if defined(_WIN32)
      _close(fd);
#else
      ::close(fd);
#endif
      *err = os_error(code, src, dst);
      return false;
    }
    done += static_cast<size_t>(n);
  }
#if defined(_WIN32)
  _close(fd);
#else
  ::close(fd);
#endif
  return true;
}

bool SystemFiles::remove(const std::string& path, OsError* err) {
#if defined(_WIN32)
  if (!DeleteFileW(wide(path).c_str())) {
    *err = win_error(GetLastError(), path, std::nullopt);
    return false;
  }
  return true;
#else
  if (::unlink(path.c_str()) != 0) {
    *err = os_error(errno, path);
    return false;
  }
  return true;
#endif
}

bool SystemFiles::exists(const std::string& path) {
  if (path.find('\0') != std::string::npos) return false;
#if defined(_WIN32)
  return GetFileAttributesW(wide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
  struct stat s;
  return ::stat(path.c_str(), &s) == 0;
#endif
}

bool SystemFiles::lexists(const std::string& path) {
  if (path.find('\0') != std::string::npos) return false;
#if defined(_WIN32)
  return GetFileAttributesW(wide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
  struct stat s;
  return ::lstat(path.c_str(), &s) == 0;
#endif
}

bool SystemFiles::listdir(const std::string& path, std::vector<std::string>* names, OsError* err) {
  names->clear();
#if defined(_WIN32)
  std::wstring pattern = wide(path.empty() ? std::string(".") : path);
  if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/' && pattern.back() != L':') pattern += L'\\';
  pattern += L'*';
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) {
    DWORD code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND) return true;
    *err = win_error(code, path, std::nullopt);
    return false;
  }
  do {
    std::wstring w(fd.cFileName);
    if (w == L"." || w == L"..") continue;
    int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(m), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], m, nullptr, nullptr);
    names->push_back(s);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return true;
#else
  DIR* d = ::opendir(path.empty() ? "." : path.c_str());
  if (!d) {
    *err = os_error(errno, path);
    return false;
  }
  while (struct dirent* e = ::readdir(d)) {
    std::string name(e->d_name);
    if (name == "." || name == "..") continue;
    names->push_back(name);
  }
  ::closedir(d);
  return true;
#endif
}

bool SystemFiles::stat(const std::string& path, FileStat* st, OsError* err) {
#if defined(_WIN32)
  struct _stat64 s;
  if (_wstat64(wide(path).c_str(), &s) != 0) {
    *err = os_error(errno, path);
    return false;
  }
  st->mtime_ns = static_cast<int64_t>(s.st_mtime) * 1000000000;
  HANDLE h = CreateFileW(wide(path).c_str(), FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  FileStat precise;
  bool have_precise = handle_stat(h, &precise);
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
#else
  struct stat s;
  if (::stat(path.c_str(), &s) != 0) {
    *err = os_error(errno, path);
    return false;
  }
#if defined(__APPLE__)
  st->mtime_ns = static_cast<int64_t>(s.st_mtimespec.tv_sec) * 1000000000 + s.st_mtimespec.tv_nsec;
#else
  st->mtime_ns = static_cast<int64_t>(s.st_mtim.tv_sec) * 1000000000 + s.st_mtim.tv_nsec;
#endif
#endif
  st->size = static_cast<uint64_t>(s.st_size);
  st->mode = static_cast<uint32_t>(s.st_mode);
  st->ino = static_cast<uint64_t>(s.st_ino);
#if defined(_WIN32)
  if (have_precise) {
    st->mtime_ns = precise.mtime_ns;
    st->ino = precise.ino;
  }
#endif
  return true;
}

bool SystemFiles::write_atomic(const std::string& path, std::string_view data, bool private_file, int64_t owner_uid,
                               int64_t owner_gid, FileStat* st, OsError* err) {
  const std::string tmp = path + ".tmp";
  std::string abs;
  if (!absolute(path, &abs, err)) return false;
  if (!makedirs(dirname_of(abs), err)) return false;
#if defined(_WIN32)
  (void)owner_uid;
  (void)owner_gid;
  if (_wunlink(wide(tmp).c_str()) != 0 && errno != ENOENT) {
    *err = os_error(errno, tmp);
    return false;
  }
  int fd = _wopen(wide(tmp).c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                  private_file ? (_S_IREAD | _S_IWRITE) : (_S_IREAD | _S_IWRITE));
  if (fd < 0) {
    *err = os_error(errno, tmp);
    return false;
  }
  auto fail = [&](OsError e) {
    _close(fd);
    _wunlink(wide(tmp).c_str());
    *err = std::move(e);
    return false;
  };
  size_t off = 0;
  while (off < data.size()) {
    size_t chunk = data.size() - off > 0x40000000 ? 0x40000000 : data.size() - off;
    int n = _write(fd, data.data() + off, static_cast<unsigned>(chunk));
    if (n < 0) return fail(os_error(errno));
    off += static_cast<size_t>(n);
  }
  if (_commit(fd) != 0) return fail(os_error(errno));
  struct _stat64 s;
  if (_fstat64(fd, &s) != 0) return fail(os_error(errno));
  FileStat precise;
  bool have_precise = fd_stat(fd, &precise);
  if (_close(fd) != 0) {
    int code = errno;
    _wunlink(wide(tmp).c_str());
    *err = os_error(code);
    return false;
  }
  if (!MoveFileExW(wide(tmp).c_str(), wide(path).c_str(), MOVEFILE_REPLACE_EXISTING)) {
    DWORD code = GetLastError();
    _wunlink(wide(tmp).c_str());
    *err = win_error(code, tmp, path);
    return false;
  }
  st->size = static_cast<uint64_t>(s.st_size);
  st->mode = static_cast<uint32_t>(s.st_mode);
  st->mtime_ns = have_precise ? precise.mtime_ns : static_cast<int64_t>(s.st_mtime) * 1000000000;
  st->ino = have_precise ? precise.ino : 0;
  return true;
#else
  if (::unlink(tmp.c_str()) != 0 && errno != ENOENT) {
    *err = os_error(errno, tmp);
    return false;
  }
  int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  int fd = ::open(tmp.c_str(), flags, private_file ? 0600 : 0666);
  if (fd < 0) {
    *err = os_error(errno, tmp);
    return false;
  }
  auto fail = [&](int code) {
    ::close(fd);
    ::unlink(tmp.c_str());
    *err = os_error(code);
    return false;
  };
  if (private_file && fchmod(fd, 0600) != 0) return fail(errno);
  if (owner_uid >= 0 || owner_gid >= 0) {
    if (fchown(fd, static_cast<uid_t>(owner_uid), static_cast<gid_t>(owner_gid)) != 0) return fail(errno);
  }
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return fail(errno);
    }
    off += static_cast<size_t>(n);
  }
  if (fsync(fd) != 0) return fail(errno);
  struct stat s;
  if (fstat(fd, &s) != 0) return fail(errno);
  if (::close(fd) != 0) {
    int code = errno;
    ::unlink(tmp.c_str());
    *err = os_error(code);
    return false;
  }
  if (::rename(tmp.c_str(), path.c_str()) != 0) {
    int code = errno;
    ::unlink(tmp.c_str());
    *err = os_error(code, tmp, path);
    return false;
  }
  st->size = static_cast<uint64_t>(s.st_size);
  st->mode = static_cast<uint32_t>(s.st_mode);
#if defined(__APPLE__)
  st->mtime_ns = static_cast<int64_t>(s.st_mtimespec.tv_sec) * 1000000000 + s.st_mtimespec.tv_nsec;
#else
  st->mtime_ns = static_cast<int64_t>(s.st_mtim.tv_sec) * 1000000000 + s.st_mtim.tv_nsec;
#endif
  st->ino = static_cast<uint64_t>(s.st_ino);
  return true;
#endif
}

bool SystemFiles::abspath(const std::string& path, std::string* out, OsError* err) {
  return absolute(path, out, err);
}

#if defined(_WIN32)
int SystemFiles::text_code_page() {
  UINT acp = GetACP();
  return acp == CP_UTF8 ? 0 : static_cast<int>(acp);
}

bool SystemFiles::decode_code_page(int code_page, const std::string& data, std::string* out, std::string* error) {
  out->clear();
  if (data.empty()) return true;
  int n = MultiByteToWideChar(static_cast<UINT>(code_page), MB_ERR_INVALID_CHARS, data.data(),
                              static_cast<int>(data.size()), nullptr, 0);
  if (n <= 0) {
    *error = "'cp" + std::to_string(code_page) + "' codec can't decode the file";
    return false;
  }
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(static_cast<UINT>(code_page), MB_ERR_INVALID_CHARS, data.data(), static_cast<int>(data.size()),
                      &w[0], n);
  int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
  out->assign(static_cast<size_t>(m), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), n, &(*out)[0], m, nullptr, nullptr);
  return true;
}

std::string SystemFiles::dirname(const std::string& path) { return dirname_of(path); }

std::string SystemFiles::join(const std::string& a, const std::string& b) {
  if (!b.empty() && (b[0] == '\\' || b[0] == '/' || (b.size() >= 2 && b[1] == ':'))) return b;
  if (a.empty() || a.back() == '\\' || a.back() == '/' || a.back() == ':') return a + b;
  return a + "\\" + b;
}

namespace {
std::string nt_normcase(const std::string& s) {
  std::wstring w = wide(s);
  for (wchar_t& c : w)
    if (c == L'/') c = L'\\';
  if (w.empty()) return s;
  int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.data(), static_cast<int>(w.size()), nullptr, 0,
                        nullptr, nullptr, 0);
  std::wstring low(static_cast<size_t>(n > 0 ? n : 0), L'\0');
  if (n > 0)
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.data(), static_cast<int>(w.size()), &low[0], n, nullptr,
                  nullptr, 0);
  int m = WideCharToMultiByte(CP_UTF8, 0, low.data(), static_cast<int>(low.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(m > 0 ? m : 0), '\0');
  if (m > 0) WideCharToMultiByte(CP_UTF8, 0, low.data(), static_cast<int>(low.size()), &out[0], m, nullptr, nullptr);
  return out;
}

std::vector<std::string> nt_parts(const std::string& rest) {
  std::vector<std::string> parts;
  size_t i = 0;
  while (i <= rest.size()) {
    size_t j = rest.find('\\', i);
    if (j == std::string::npos) j = rest.size();
    if (j > i) parts.push_back(rest.substr(i, j - i));
    i = j + 1;
  }
  return parts;
}
}

bool SystemFiles::relpath(const std::string& path, const std::string& start, std::string* out, OsError* err) {
  if (path.empty()) {
    err->code = 0;
    err->message = "no path specified";
    return false;
  }
  std::string start_abs, path_abs;
  if (!absolute(start, &start_abs, err) || !absolute(path, &path_abs, err)) return false;
  const py::NtPaths nt;
  std::string start_drive, start_root, start_rest, path_drive, path_root, path_rest;
  nt.splitroot(start_abs, &start_drive, &start_root, &start_rest);
  nt.splitroot(path_abs, &path_drive, &path_root, &path_rest);
  if (nt_normcase(start_drive) != nt_normcase(path_drive)) {
    err->code = 0;
    err->message = "path is on mount " + py::repr(path_drive) + ", start on mount " + py::repr(start_drive);
    return false;
  }
  std::vector<std::string> start_list = nt_parts(start_rest), path_list = nt_parts(path_rest);
  size_t i = 0;
  while (i < start_list.size() && i < path_list.size() && nt_normcase(start_list[i]) == nt_normcase(path_list[i])) i++;
  std::vector<std::string> rel(start_list.size() - i, "..");
  rel.insert(rel.end(), path_list.begin() + static_cast<std::ptrdiff_t>(i), path_list.end());
  if (rel.empty()) {
    *out = ".";
    return true;
  }
  std::string r;
  for (const std::string& part : rel) r = r.empty() ? part : join(r, part);
  *out = r;
  return true;
}
#endif

}

namespace plat {

namespace {

Error from_os(const OsError& e) {
  Error out;
  out.code = e.code;
  out.text = e.message;
  out.winerror = e.winerror;
  return out;
}

}

std::string dir_name(const std::string& path) { return platform::dirname_of(path); }

bool abs_path(const std::string& path, std::string* out, Error* err) {
  OsError e;
  if (platform::absolute(path, out, &e)) return true;
  *err = from_os(e);
  return false;
}

bool make_dirs(const std::string& path, Error* err) {
  OsError e;
  if (path.empty() || platform::makedirs(path, &e)) return true;
  *err = from_os(e);
  return false;
}

}
}
