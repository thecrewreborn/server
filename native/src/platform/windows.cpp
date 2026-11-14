#if defined(_WIN32)

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <mstcpip.h>
#include <objbase.h>
#include <shellapi.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "platform/platform.h"

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#ifndef BCRYPT_USE_SYSTEM_PREFERRED_RNG
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002
#endif

namespace tcr::plat {

namespace {

std::wstring widen(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
  return s;
}

Error win_error(DWORD code) {
  Error e;
  e.code = static_cast<int>(code);
  e.winerror = true;
  wchar_t* buf = nullptr;
  DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buf),
                           0, nullptr);
  if (n && buf) {
    std::wstring text(buf, n);
    while (!text.empty() && (text.back() <= L' ' || text.back() == L'.')) text.pop_back();
    e.text = narrow(text);
  }
  if (buf) LocalFree(buf);
  return e;
}

Error crt_error(DWORD code) {
  Error e;
  e.winerror = false;
  switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
      e.code = ENOENT;
      e.text = "No such file or directory";
      break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
      e.code = EACCES;
      e.text = "Permission denied";
      break;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      e.code = EEXIST;
      e.text = "File exists";
      break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
      e.code = ENOSPC;
      e.text = "No space left on device";
      break;
    case ERROR_TOO_MANY_OPEN_FILES:
      e.code = EMFILE;
      e.text = "Too many open files";
      break;
    default:
      e.code = EINVAL;
      e.text = "Invalid argument";
      break;
  }
  return e;
}

void set_wsa(Error* err) {
  if (err) *err = win_error(static_cast<DWORD>(WSAGetLastError()));
}
void set_win(Error* err) {
  if (err) *err = win_error(GetLastError());
}
void set_crt(Error* err) {
  if (err) *err = crt_error(GetLastError());
}

bool fill_addr(const char* ip, int port, sockaddr_in* sa) {
  memset(sa, 0, sizeof(*sa));
  sa->sin_family = AF_INET;
  sa->sin_port = htons(static_cast<u_short>(port));
  if (!ip || !*ip) {
    sa->sin_addr.s_addr = htonl(INADDR_ANY);
    return true;
  }
  return inet_pton(AF_INET, ip, &sa->sin_addr) == 1;
}

Addr to_addr(const sockaddr_in& sa) {
  char text[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, const_cast<in_addr*>(&sa.sin_addr), text, sizeof(text));
  Addr a;
  a.ip = text;
  a.port = ntohs(sa.sin_port);
  return a;
}

int wait_socket(Socket s, short events, double seconds, Error* err) {
  double deadline = seconds >= 0 ? monotonic() + seconds : 0;
  for (;;) {
    WSAPOLLFD p;
    p.fd = static_cast<SOCKET>(s);
    p.events = events;
    p.revents = 0;
    int ms = -1;
    if (seconds >= 0) {
      double left = deadline - monotonic();
      if (left < 0) left = 0;
      ms = static_cast<int>(std::ceil(left * 1000.0));
    }
    int r = WSAPoll(&p, 1, ms);
    if (r > 0) return 1;
    if (r == 0) return 0;
    if (WSAGetLastError() == WSAEINTR) continue;
    set_wsa(err);
    return -1;
  }
}

}

ErrorKind error_kind(const Error& e) {
  if (e.winerror) {
    switch (e.code) {
      case WSAETIMEDOUT:
      case ERROR_SEM_TIMEOUT:
        return ErrorKind::kTimeout;
      case WSAECONNRESET:
      case ERROR_NETNAME_DELETED:
        return ErrorKind::kReset;
      case WSAESHUTDOWN:
      case ERROR_BROKEN_PIPE:
        return ErrorKind::kBrokenPipe;
      case WSAECONNABORTED:
        return ErrorKind::kAborted;
      case WSAECONNREFUSED:
        return ErrorKind::kRefused;
      case WSAEADDRINUSE:
        return ErrorKind::kAddrInUse;
      case WSAEACCES:
      case ERROR_ACCESS_DENIED:
        return ErrorKind::kAccess;
      case ERROR_FILE_NOT_FOUND:
      case ERROR_PATH_NOT_FOUND:
        return ErrorKind::kNotFound;
      default:
        return ErrorKind::kOther;
    }
  }
  switch (e.code) {
    case EACCES:
      return ErrorKind::kAccess;
    case ENOENT:
      return ErrorKind::kNotFound;
    case EPIPE:
      return ErrorKind::kBrokenPipe;
    default:
      return ErrorKind::kOther;
  }
}

const char* python_error_name(const Error& e) {
  switch (error_kind(e)) {
    case ErrorKind::kTimeout:
      return "TimeoutError";
    case ErrorKind::kReset:
      return "ConnectionResetError";
    case ErrorKind::kBrokenPipe:
      return "BrokenPipeError";
    case ErrorKind::kAborted:
      return "ConnectionAbortedError";
    case ErrorKind::kRefused:
      return "ConnectionRefusedError";
    case ErrorKind::kAccess:
      return "PermissionError";
    case ErrorKind::kNotFound:
      return "FileNotFoundError";
    default:
      return "OSError";
  }
}

std::string python_error_text(const Error& e) {
  return (e.winerror ? "[WinError " : "[Errno ") + std::to_string(e.code) + "] " + e.text;
}

bool is_addr_in_use(const Error& e) { return e.code == WSAEADDRINUSE; }

Error system_error(int code) { return win_error(static_cast<DWORD>(code)); }

bool init() {
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

Socket tcp_listen(const char* ip, int port, int backlog, bool reuse, Error* err) {
  sockaddr_in sa;
  if (!fill_addr(ip, port, &sa)) {
    if (err) *err = win_error(WSAEINVAL);
    return kNoSocket;
  }
  SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
  if (s == INVALID_SOCKET) {
    set_wsa(err);
    return kNoSocket;
  }
  if (reuse) {
    BOOL one = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
  }
  if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 || listen(s, backlog) != 0) {
    set_wsa(err);
    closesocket(s);
    return kNoSocket;
  }
  return static_cast<Socket>(s);
}

Socket tcp_accept(Socket listener, double seconds, Addr* peer, Error* err) {
  if (err) *err = Error();
  int r = wait_socket(listener, POLLRDNORM, seconds, err);
  if (r <= 0) return kNoSocket;
  sockaddr_in sa;
  int len = sizeof(sa);
  SOCKET s = accept(static_cast<SOCKET>(listener), reinterpret_cast<sockaddr*>(&sa), &len);
  if (s == INVALID_SOCKET) {
    int code = WSAGetLastError();
    if (code == WSAEWOULDBLOCK || code == WSAEINTR) return kNoSocket;
    set_wsa(err);
    return kNoSocket;
  }
  u_long one = 1;
  ioctlsocket(s, FIONBIO, &one);
  if (peer) *peer = to_addr(sa);
  return static_cast<Socket>(s);
}

Addr local_addr(Socket s) {
  sockaddr_in sa;
  int len = sizeof(sa);
  memset(&sa, 0, sizeof(sa));
  getsockname(static_cast<SOCKET>(s), reinterpret_cast<sockaddr*>(&sa), &len);
  return to_addr(sa);
}

int wait_readable(Socket s, double seconds, Error* err) { return wait_socket(s, POLLRDNORM, seconds, err); }
int wait_writable(Socket s, double seconds, Error* err) { return wait_socket(s, POLLWRNORM, seconds, err); }

long sock_recv(Socket s, void* buf, size_t n, Error* err) {
  int want = n > 0x7fffffff ? 0x7fffffff : static_cast<int>(n);
  int r = recv(static_cast<SOCKET>(s), static_cast<char*>(buf), want, 0);
  if (r >= 0) return r;
  if (WSAGetLastError() == WSAEWOULDBLOCK) return kWouldBlock;
  set_wsa(err);
  return -1;
}

long sock_send(Socket s, const void* buf, size_t n, Error* err) {
  int want = n > 0x7fffffff ? 0x7fffffff : static_cast<int>(n);
  int r = send(static_cast<SOCKET>(s), static_cast<const char*>(buf), want, 0);
  if (r >= 0) return r;
  if (WSAGetLastError() == WSAEWOULDBLOCK) return kWouldBlock;
  set_wsa(err);
  return -1;
}

void shutdown_write(Socket s) { shutdown(static_cast<SOCKET>(s), SD_SEND); }

void close_socket(Socket s) {
  if (s != kNoSocket) closesocket(static_cast<SOCKET>(s));
}

Socket udp_open(const char* ip, int port, bool reuse, bool broadcast, Error* err) {
  sockaddr_in sa;
  if (!fill_addr(ip, port, &sa)) {
    if (err) *err = win_error(WSAEINVAL);
    return kNoSocket;
  }
  SOCKET s = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
  if (s == INVALID_SOCKET) {
    set_wsa(err);
    return kNoSocket;
  }
  BOOL one = TRUE;
  if (reuse) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
  if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof(one));
  if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
    set_wsa(err);
    closesocket(s);
    return kNoSocket;
  }
  BOOL off = FALSE;
  DWORD bytes = 0;
  WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &bytes, nullptr, nullptr);
  return static_cast<Socket>(s);
}

void udp_report_connreset(Socket s) {
  BOOL on = TRUE;
  DWORD bytes = 0;
  WSAIoctl(static_cast<SOCKET>(s), SIO_UDP_CONNRESET, &on, sizeof(on), nullptr, 0, &bytes, nullptr, nullptr);
}

bool udp_port_free(int port) {
  Error err;
  Socket s = udp_open("0.0.0.0", port, false, false, &err);
  if (s == kNoSocket) return false;
  close_socket(s);
  return true;
}

long udp_recvfrom(Socket s, void* buf, size_t n, Addr* from, Error* err) {
  sockaddr_in sa;
  int len = sizeof(sa);
  memset(&sa, 0, sizeof(sa));
  int want = n > 0x7fffffff ? 0x7fffffff : static_cast<int>(n);
  int r = recvfrom(static_cast<SOCKET>(s), static_cast<char*>(buf), want, 0, reinterpret_cast<sockaddr*>(&sa), &len);
  if (r >= 0) {
    if (from) *from = to_addr(sa);
    return r;
  }
  set_wsa(err);
  return -1;
}

long udp_sendto(Socket s, const void* buf, size_t n, const Addr& to, Error* err) {
  sockaddr_in sa;
  if (!fill_addr(to.ip.c_str(), to.port, &sa)) {
    if (err) *err = win_error(WSAEINVAL);
    return -1;
  }
  int r = sendto(static_cast<SOCKET>(s), static_cast<const char*>(buf), static_cast<int>(n), 0,
                 reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
  if (r >= 0) return r;
  set_wsa(err);
  return -1;
}

bool route_ip(const std::string& peer, int port, std::string* ip, Error* err) {
  sockaddr_in sa;
  if (!fill_addr(peer.c_str(), port, &sa)) {
    if (err) *err = win_error(WSAEINVAL);
    return false;
  }
  SOCKET s = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
  if (s == INVALID_SOCKET) {
    set_wsa(err);
    return false;
  }
  if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
    set_wsa(err);
    closesocket(s);
    return false;
  }
  *ip = local_addr(static_cast<Socket>(s)).ip;
  closesocket(s);
  return true;
}

namespace {

bool first_ipv4(const char* host, const char* service, int socktype, std::string* ip, std::string* line) {
  addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = socktype;
  addrinfo* found = nullptr;
  int code = getaddrinfo(host, service, &hints, &found);
  if (code != 0 || !found) {
    *line = "socket.gaierror: [Errno " + std::to_string(code ? code : WSAHOST_NOT_FOUND) + "] getaddrinfo failed";
    if (found) freeaddrinfo(found);
    return false;
  }
  *ip = to_addr(*reinterpret_cast<const sockaddr_in*>(found->ai_addr)).ip;
  freeaddrinfo(found);
  return true;
}

}

bool resolve_ipv4(const std::string& host, int port, std::string* ip, std::string* error) {
  std::string line;
  if (first_ipv4(host.c_str(), std::to_string(port).c_str(), SOCK_DGRAM, ip, &line)) return true;
  *error = line.substr(line.find(": ") + 2);
  return false;
}

bool bind_address(const std::string& host, std::string* ip, std::string* error_line) {
  in_addr numeric;
  if (host.empty()) {
    *ip = "0.0.0.0";
  } else if (host == "255.255.255.255" || host == "<broadcast>") {
    *ip = "255.255.255.255";
  } else if (inet_pton(AF_INET, host.c_str(), &numeric) == 1) {
    *ip = host;
  } else {
    return first_ipv4(host.c_str(), nullptr, 0, ip, error_line);
  }
  return true;
}

namespace {
std::wstring quote_arg(const std::wstring& a) {
  if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
  std::wstring out = L"\"";
  for (size_t i = 0;; ++i) {
    size_t slashes = 0;
    while (i < a.size() && a[i] == L'\\') {
      ++i;
      ++slashes;
    }
    if (i == a.size()) {
      out.append(slashes * 2, L'\\');
      break;
    }
    if (a[i] == L'"') {
      out.append(slashes * 2 + 1, L'\\');
    } else {
      out.append(slashes, L'\\');
    }
    out.push_back(a[i]);
  }
  out.push_back(L'"');
  return out;
}
const wchar_t kRestartVar[] = L"TCREBORN_RESTART_PARENT";
}

bool restart_self(const std::vector<std::string>& args, Error* err) {
  std::wstring exe = widen(executable_path());
  std::wstring cmd = quote_arg(exe);
  for (const std::string& a : args) cmd += L" " + quote_arg(widen(a));
  std::fflush(stdout);
  std::fflush(stderr);
  SetEnvironmentVariableW(kRestartVar, std::to_wstring(GetCurrentProcessId()).c_str());
  std::vector<HANDLE> std_handles;
  for (DWORD which : {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
    HANDLE h = GetStdHandle(which);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) continue;
    if (!SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) continue;
    bool seen = false;
    for (HANDLE o : std_handles) seen |= o == h;
    if (!seen) std_handles.push_back(h);
  }
  STARTUPINFOEXW si;
  ZeroMemory(&si, sizeof(si));
  si.StartupInfo.cb = sizeof(si);
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::vector<unsigned char> attrs(size);
  si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
  bool have_list = !std_handles.empty() && InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size) &&
                   UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                             std_handles.data(), std_handles.size() * sizeof(HANDLE), nullptr,
                                             nullptr);
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));
  std::vector<wchar_t> line(cmd.begin(), cmd.end());
  line.push_back(L'\0');
  BOOL ok = CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, have_list ? TRUE : FALSE,
                           have_list ? EXTENDED_STARTUPINFO_PRESENT : 0, nullptr, nullptr, &si.StartupInfo, &pi);
  DWORD code = ok ? 0 : GetLastError();
  if (have_list) DeleteProcThreadAttributeList(si.lpAttributeList);
  SetEnvironmentVariableW(kRestartVar, nullptr);
  if (!ok) {
    if (err) *err = win_error(code);
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

void wait_restart_parent() {
  std::string pid = env_var("TCREBORN_RESTART_PARENT");
  SetEnvironmentVariableW(kRestartVar, nullptr);
  if (pid.empty()) return;
  DWORD id = static_cast<DWORD>(std::strtoul(pid.c_str(), nullptr, 10));
  HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, id);
  if (!h) return;
  WaitForSingleObject(h, 30000);
  CloseHandle(h);
}

namespace {
volatile LONG g_interrupted = 0;
BOOL WINAPI on_console_event(DWORD kind) {
  if (kind == CTRL_C_EVENT || kind == CTRL_CLOSE_EVENT || kind == CTRL_LOGOFF_EVENT || kind == CTRL_SHUTDOWN_EVENT) {
    InterlockedExchange(&g_interrupted, 1);
    return TRUE;
  }
  return FALSE;
}
}

void catch_interrupt() { SetConsoleCtrlHandler(on_console_event, TRUE); }

bool interrupted() { return InterlockedCompareExchange(&g_interrupted, 0, 0) != 0; }

namespace {
struct ThreadStart {
  void (*fn)(void*);
  void* arg;
};

DWORD WINAPI thread_main(LPVOID p) {
  ThreadStart start = *static_cast<ThreadStart*>(p);
  delete static_cast<ThreadStart*>(p);
  start.fn(start.arg);
  return 0;
}
}

bool start_thread(void (*fn)(void*), void* arg) {
  ThreadStart* start = new ThreadStart{fn, arg};
  HANDLE h = CreateThread(nullptr, 1024 * 1024, thread_main, start, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (!h) {
    delete start;
    return false;
  }
  CloseHandle(h);
  return true;
}

static_assert(sizeof(SRWLOCK) <= 64, "Mutex storage too small");

Mutex::Mutex() { InitializeSRWLock(reinterpret_cast<SRWLOCK*>(storage_)); }
Mutex::~Mutex() {}
void Mutex::lock() { AcquireSRWLockExclusive(reinterpret_cast<SRWLOCK*>(storage_)); }
void Mutex::unlock() { ReleaseSRWLockExclusive(reinterpret_cast<SRWLOCK*>(storage_)); }

double monotonic() {
  static LARGE_INTEGER freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f;
  }();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
}

double wall_time() {
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  ULARGE_INTEGER t;
  t.LowPart = ft.dwLowDateTime;
  t.HighPart = ft.dwHighDateTime;
  return static_cast<double>(t.QuadPart - 116444736000000000ULL) / 1e7;
}

void sleep_seconds(double seconds) {
  if (seconds <= 0) return;
  Sleep(static_cast<DWORD>(seconds * 1000.0));
}

int64_t wall_time_ns() {
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  ULARGE_INTEGER t;
  t.LowPart = ft.dwLowDateTime;
  t.HighPart = ft.dwHighDateTime;
  return static_cast<int64_t>(t.QuadPart - 116444736000000000ULL) * 100;
}

LocalTime local_now() { return local_from_ns(wall_time_ns()); }

LocalTime local_from_ns(int64_t ns) {
  int64_t secs = ns / 1000000000, rem = ns % 1000000000;
  if (rem < 0) {
    rem += 1000000000;
    secs -= 1;
  }
  __time64_t t = static_cast<__time64_t>(secs);
  tm parts;
  _localtime64_s(&parts, &t);
  return LocalTime{parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec,
                   static_cast<int>(rem / 1000)};
}

bool random_bytes(uint8_t* out, size_t n) {
  while (n > 0) {
    ULONG take = n > 0x10000000 ? 0x10000000 : static_cast<ULONG>(n);
    if (BCryptGenRandom(nullptr, out, take, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return false;
    out += take;
    n -= take;
  }
  return true;
}

namespace {
bool write_all_handle(HANDLE h, const void* data, size_t n, Error* err) {
  const char* p = static_cast<const char*>(data);
  while (n > 0) {
    DWORD chunk = n > 0x40000000 ? 0x40000000 : static_cast<DWORD>(n);
    DWORD written = 0;
    if (!WriteFile(h, p, chunk, &written, nullptr)) {
      set_crt(err);
      return false;
    }
    p += written;
    n -= written;
  }
  return true;
}
}

bool write_atomic(const std::string& path, const void* data, size_t n, bool private_file, Error* err) {
  (void)private_file;
  std::string tmp = path + ".tmp";
  std::string abs;
  if (!abs_path(path, &abs, err) || !make_dirs(dir_name(abs), err)) return false;
  std::wstring wtmp = widen(tmp);
  if (!DeleteFileW(wtmp.c_str())) {
    DWORD code = GetLastError();
    if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
      if (err) *err = win_error(code);
      return false;
    }
  }
  HANDLE h = CreateFileW(wtmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    set_crt(err);
    return false;
  }
  bool ok = write_all_handle(h, data, n, err);
  if (ok && !FlushFileBuffers(h)) {
    set_crt(err);
    ok = false;
  }
  CloseHandle(h);
  if (ok && !MoveFileExW(wtmp.c_str(), widen(path).c_str(), MOVEFILE_REPLACE_EXISTING)) {
    set_win(err);
    ok = false;
  }
  if (!ok) DeleteFileW(wtmp.c_str());
  return ok;
}

bool write_file(const std::string& path, const void* data, size_t n, Error* err) {
  HANDLE h = CreateFileW(widen(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    set_crt(err);
    return false;
  }
  bool ok = write_all_handle(h, data, n, err);
  CloseHandle(h);
  return ok;
}

bool read_file(const std::string& path, std::string* out, Error* err) {
  HANDLE h = CreateFileW(widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    set_crt(err);
    return false;
  }
  out->clear();
  char buf[65536];
  for (;;) {
    DWORD got = 0;
    if (!ReadFile(h, buf, sizeof(buf), &got, nullptr)) {
      set_crt(err);
      CloseHandle(h);
      return false;
    }
    if (got == 0) break;
    out->append(buf, got);
  }
  CloseHandle(h);
  return true;
}

AppendFile::~AppendFile() { close(); }

bool AppendFile::open(const std::string& path, Error* err) {
  close();
  HANDLE h = CreateFileW(widen(path).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    set_crt(err);
    return false;
  }
  handle_ = reinterpret_cast<intptr_t>(h);
  return true;
}

long long AppendFile::size(Error* err) {
  LARGE_INTEGER size;
  if (!GetFileSizeEx(reinterpret_cast<HANDLE>(handle_), &size)) {
    set_crt(err);
    return -1;
  }
  return size.QuadPart;
}

bool AppendFile::write(const void* data, size_t n, Error* err) {
  return write_all_handle(reinterpret_cast<HANDLE>(handle_), data, n, err);
}

void AppendFile::close() {
  if (handle_ != -1) CloseHandle(reinterpret_cast<HANDLE>(handle_));
  handle_ = -1;
}

bool replace_file(const std::string& src, const std::string& dst, Error* err) {
  if (!MoveFileExW(widen(src).c_str(), widen(dst).c_str(), MOVEFILE_REPLACE_EXISTING)) {
    set_win(err);
    return false;
  }
  return true;
}

bool remove_file(const std::string& path, Error* err) {
  if (!DeleteFileW(widen(path).c_str())) {
    set_win(err);
    return false;
  }
  return true;
}

bool path_exists(const std::string& path) {
  return GetFileAttributesW(widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool is_file(const std::string& path) {
  DWORD a = GetFileAttributesW(widen(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool is_dir(const std::string& path) {
  DWORD a = GetFileAttributesW(widen(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool list_dir(const std::string& path, std::vector<std::string>* names, Error* err) {
  WIN32_FIND_DATAW data;
  HANDLE h = FindFirstFileW(widen(join_path(path, "*")).c_str(), &data);
  if (h == INVALID_HANDLE_VALUE) {
    set_win(err);
    return false;
  }
  names->clear();
  do {
    std::string name = narrow(data.cFileName);
    if (name != "." && name != "..") names->push_back(name);
  } while (FindNextFileW(h, &data));
  FindClose(h);
  return true;
}

std::string join_path(const std::string& a, const std::string& b) {
  if (!b.empty() && (b[0] == '/' || b[0] == '\\' || (b.size() >= 2 && b[1] == ':'))) return b;
  if (a.empty() || a.back() == '/' || a.back() == '\\' || (a.size() == 2 && a[1] == ':')) return a + b;
  return a + "\\" + b;
}

std::string executable_path() {
  std::wstring buf(32768, L'\0');
  DWORD n = GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
  buf.resize(n);
  return narrow(buf);
}

std::string env_var(const char* name) {
  std::wstring wname = widen(name);
  DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
  if (!n) return "";
  std::wstring value(n, L'\0');
  n = GetEnvironmentVariableW(wname.c_str(), &value[0], n);
  value.resize(n);
  return narrow(value);
}

const char* const kTextNewline = "\r\n";

namespace {
volatile LONG g_errors_replace = 0;

std::string ansi_text(const std::string& utf8, bool backslash) {
  UINT acp = GetACP();
  if (acp == CP_UTF8) return utf8;
  std::wstring w = widen(utf8);
  std::string out;
  out.reserve(w.size());
  for (size_t i = 0; i < w.size();) {
    uint32_t cp = w[i];
    size_t units = 1;
    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size() && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (w[i + 1] - 0xDC00);
      units = 2;
    }
    if (cp < 0x80) {
      out += static_cast<char>(cp);
      i += units;
      continue;
    }
    char buf[8];
    BOOL used_default = FALSE;
    int n = units == 1 ? WideCharToMultiByte(acp, WC_NO_BEST_FIT_CHARS, &w[i], 1, buf, sizeof(buf), nullptr,
                                             &used_default)
                       : 0;
    if (n > 0 && !used_default) {
      out.append(buf, static_cast<size_t>(n));
    } else if (!backslash) {
      out += '?';
    } else {
      char esc[16];
      if (cp < 0x100) {
        std::snprintf(esc, sizeof(esc), "\\x%02x", static_cast<unsigned>(cp));
      } else if (cp < 0x10000) {
        std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(cp));
      } else {
        std::snprintf(esc, sizeof(esc), "\\U%08x", static_cast<unsigned>(cp));
      }
      out += esc;
    }
    i += units;
  }
  return out;
}

void write_line(DWORD which, const std::string& text) {
  HANDLE h = GetStdHandle(which);
  if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
  std::string line;
  line.reserve(text.size() + 2);
  for (char c : text) {
    if (c == '\n') line += '\r';
    line += c;
  }
  line += "\r\n";
  DWORD mode = 0;
  if (GetConsoleMode(h, &mode)) {
    std::wstring w = widen(line);
    DWORD written = 0;
    WriteConsoleW(h, w.data(), static_cast<DWORD>(w.size()), &written, nullptr);
    return;
  }
  bool backslash = which == STD_ERROR_HANDLE && InterlockedCompareExchange(&g_errors_replace, 0, 0) == 0;
  std::string bytes = ansi_text(line, backslash);
  DWORD written = 0;
  WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}
}

namespace {
std::atomic<ConsoleTap> g_tap{nullptr};
}

void print_line(const std::string& text) {
  write_line(STD_OUTPUT_HANDLE, text);
  if (ConsoleTap tap = g_tap.load()) tap(text, false);
}
void print_error_line(const std::string& text) {
  write_line(STD_ERROR_HANDLE, text);
  if (ConsoleTap tap = g_tap.load()) tap(text, true);
}
void stream_errors_replace() { InterlockedExchange(&g_errors_replace, 1); }
void set_console_tap(ConsoleTap tap) { g_tap.store(tap); }

bool desktop_session() {
  if (!env_var("SSH_CONNECTION").empty() || !env_var("SSH_CLIENT").empty()) return false;
  DWORD session = 0;
  if (ProcessIdToSessionId(GetCurrentProcessId(), &session) && session == 0) return false;
  HWINSTA station = GetProcessWindowStation();
  if (!station) return false;
  USEROBJECTFLAGS flags{};
  DWORD needed = 0;
  if (!GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), &needed)) return false;
  return (flags.dwFlags & WSF_VISIBLE) != 0;
}

bool open_with_desktop(const std::string& target, Error* err) {
  std::wstring w = widen(target);
  HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  HINSTANCE r = ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  DWORD code = GetLastError();
  if (SUCCEEDED(com)) CoUninitialize();
  if (reinterpret_cast<INT_PTR>(r) > 32) return true;
  if (err) *err = system_error(code ? static_cast<int>(code) : ERROR_FILE_NOT_FOUND);
  return false;
}

}

#endif
