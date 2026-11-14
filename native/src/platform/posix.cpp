#if !defined(_WIN32)

#include "platform/platform.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <Security/AuthSession.h>
#include <mach-o/dyld.h>
#include <sys/random.h>
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <new>

extern char** environ;

namespace tcr::plat {

namespace {

pthread_mutex_t g_strerror_lock = PTHREAD_MUTEX_INITIALIZER;

Error make_error(int code) {
  Error e;
  e.code = code;
  pthread_mutex_lock(&g_strerror_lock);
  const char* text = strerror(code);
  e.text = text ? text : "";
  pthread_mutex_unlock(&g_strerror_lock);
  return e;
}

void set_error(Error* err, int code) {
  if (err) *err = make_error(code);
}

bool fill_addr(const char* ip, int port, sockaddr_in* sa) {
  memset(sa, 0, sizeof(*sa));
  sa->sin_family = AF_INET;
  sa->sin_port = htons(static_cast<uint16_t>(port));
  if (!ip || !*ip) {
    sa->sin_addr.s_addr = htonl(INADDR_ANY);
    return true;
  }
  return inet_pton(AF_INET, ip, &sa->sin_addr) == 1;
}

Addr to_addr(const sockaddr_in& sa) {
  char text[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &sa.sin_addr, text, sizeof(text));
  Addr a;
  a.ip = text;
  a.port = ntohs(sa.sin_port);
  return a;
}

void set_cloexec(int fd) {
  int flags = fcntl(fd, F_GETFD);
  if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int wait_fd(int fd, short events, double seconds, Error* err) {
  double deadline = seconds >= 0 ? monotonic() + seconds : 0;
  for (;;) {
    pollfd p;
    p.fd = fd;
    p.events = events;
    p.revents = 0;
    int ms = -1;
    if (seconds >= 0) {
      double left = deadline - monotonic();
      if (left < 0) left = 0;
      ms = static_cast<int>(std::ceil(left * 1000.0));
    }
    int r = poll(&p, 1, ms);
    if (r > 0) return 1;
    if (r == 0) return 0;
    if (errno == EINTR) continue;
    set_error(err, errno);
    return -1;
  }
}

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

}

ErrorKind error_kind(const Error& e) {
  switch (e.code) {
    case ETIMEDOUT:
      return ErrorKind::kTimeout;
    case ECONNRESET:
      return ErrorKind::kReset;
    case EPIPE:
    case ESHUTDOWN:
      return ErrorKind::kBrokenPipe;
    case ECONNABORTED:
      return ErrorKind::kAborted;
    case ECONNREFUSED:
      return ErrorKind::kRefused;
    case EADDRINUSE:
      return ErrorKind::kAddrInUse;
    case EACCES:
    case EPERM:
      return ErrorKind::kAccess;
    case ENOENT:
      return ErrorKind::kNotFound;
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

std::string python_error_text(const Error& e) { return "[Errno " + std::to_string(e.code) + "] " + e.text; }

bool is_addr_in_use(const Error& e) { return e.code == EADDRINUSE; }

Error system_error(int code) { return make_error(code); }

bool init() {
  signal(SIGPIPE, SIG_IGN);
  return true;
}

Socket tcp_listen(const char* ip, int port, int backlog, bool reuse, Error* err) {
  sockaddr_in sa;
  if (!fill_addr(ip, port, &sa)) {
    set_error(err, EINVAL);
    return kNoSocket;
  }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    set_error(err, errno);
    return kNoSocket;
  }
  set_cloexec(fd);
  if (reuse) {
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  }
  if (bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 || listen(fd, backlog) != 0) {
    int code = errno;
    ::close(fd);
    set_error(err, code);
    return kNoSocket;
  }
  return fd;
}

Socket tcp_accept(Socket listener, double seconds, Addr* peer, Error* err) {
  if (err) *err = Error();
  int r = wait_fd(static_cast<int>(listener), POLLIN, seconds, err);
  if (r <= 0) return kNoSocket;
  sockaddr_in sa;
  socklen_t len = sizeof(sa);
  int fd = accept(static_cast<int>(listener), reinterpret_cast<sockaddr*>(&sa), &len);
  if (fd < 0) {
    int code = errno;
    if (code == EAGAIN || code == EWOULDBLOCK || code == EINTR) return kNoSocket;
    set_error(err, code);
    return kNoSocket;
  }
  set_cloexec(fd);
  set_nonblocking(fd);
#if defined(SO_NOSIGPIPE)
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  if (peer) *peer = to_addr(sa);
  return fd;
}

Addr local_addr(Socket s) {
  sockaddr_in sa;
  socklen_t len = sizeof(sa);
  memset(&sa, 0, sizeof(sa));
  getsockname(static_cast<int>(s), reinterpret_cast<sockaddr*>(&sa), &len);
  return to_addr(sa);
}

int wait_readable(Socket s, double seconds, Error* err) { return wait_fd(static_cast<int>(s), POLLIN, seconds, err); }

int wait_writable(Socket s, double seconds, Error* err) {
  return wait_fd(static_cast<int>(s), POLLOUT, seconds, err);
}

long sock_recv(Socket s, void* buf, size_t n, Error* err) {
  for (;;) {
    ssize_t r = recv(static_cast<int>(s), buf, n, 0);
    if (r >= 0) return static_cast<long>(r);
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return kWouldBlock;
    set_error(err, errno);
    return -1;
  }
}

long sock_send(Socket s, const void* buf, size_t n, Error* err) {
  for (;;) {
    ssize_t r = send(static_cast<int>(s), buf, n, kSendFlags);
    if (r >= 0) return static_cast<long>(r);
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return kWouldBlock;
    set_error(err, errno);
    return -1;
  }
}

void shutdown_write(Socket s) { shutdown(static_cast<int>(s), SHUT_WR); }

void close_socket(Socket s) {
  if (s != kNoSocket) ::close(static_cast<int>(s));
}

Socket udp_open(const char* ip, int port, bool reuse, bool broadcast, Error* err) {
  sockaddr_in sa;
  if (!fill_addr(ip, port, &sa)) {
    set_error(err, EINVAL);
    return kNoSocket;
  }
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    set_error(err, errno);
    return kNoSocket;
  }
  set_cloexec(fd);
  int one = 1;
  if (reuse) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (broadcast) setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
  if (bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
    int code = errno;
    ::close(fd);
    set_error(err, code);
    return kNoSocket;
  }
  return fd;
}

void udp_report_connreset(Socket) {}

bool udp_port_free(int port) {
  Error err;
  Socket s = udp_open("0.0.0.0", port, false, false, &err);
  if (s == kNoSocket) return false;
  close_socket(s);
  return true;
}

long udp_recvfrom(Socket s, void* buf, size_t n, Addr* from, Error* err) {
  for (;;) {
    sockaddr_in sa;
    socklen_t len = sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    ssize_t r = recvfrom(static_cast<int>(s), buf, n, 0, reinterpret_cast<sockaddr*>(&sa), &len);
    if (r >= 0) {
      if (from) *from = to_addr(sa);
      return static_cast<long>(r);
    }
    if (errno == EINTR) continue;
    set_error(err, errno);
    return -1;
  }
}

long udp_sendto(Socket s, const void* buf, size_t n, const Addr& to, Error* err) {
  sockaddr_in sa;
  if (!fill_addr(to.ip.c_str(), to.port, &sa)) {
    set_error(err, EINVAL);
    return -1;
  }
  for (;;) {
    ssize_t r = sendto(static_cast<int>(s), buf, n, kSendFlags, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    if (r >= 0) return static_cast<long>(r);
    if (errno == EINTR) continue;
    set_error(err, errno);
    return -1;
  }
}

bool route_ip(const std::string& peer, int port, std::string* ip, Error* err) {
  sockaddr_in sa;
  if (!fill_addr(peer.c_str(), port, &sa)) {
    set_error(err, EINVAL);
    return false;
  }
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    set_error(err, errno);
    return false;
  }
  if (connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
    int code = errno;
    ::close(fd);
    set_error(err, code);
    return false;
  }
  *ip = local_addr(fd).ip;
  ::close(fd);
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
    if (code == EAI_SYSTEM) {
      Error e = make_error(errno);
      *line = std::string(python_error_name(e)) + ": " + python_error_text(e);
    } else {
      *line = "socket.gaierror: [Errno " + std::to_string(code) + "] " + gai_strerror(code);
    }
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
volatile sig_atomic_t g_interrupted = 0;
extern "C" void on_sigint(int) { g_interrupted = 1; }
}

void catch_interrupt() {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_sigint;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}

bool interrupted() { return g_interrupted != 0; }

bool restart_self(const std::vector<std::string>& args, Error* err) {
  std::string exe = executable_path();
  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(exe.c_str()));
  for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  std::fflush(stdout);
  std::fflush(stderr);
  long max = sysconf(_SC_OPEN_MAX);
  if (max < 0 || max > 65536) max = 65536;
  for (int fd = 3; fd < max; ++fd) close(fd);
  signal(SIGPIPE, SIG_DFL);
  signal(SIGINT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  sigset_t none;
  sigemptyset(&none);
  pthread_sigmask(SIG_SETMASK, &none, nullptr);
  execv(exe.c_str(), argv.data());
  set_error(err, errno);
  return false;
}

void wait_restart_parent() {}

namespace {
struct ThreadStart {
  void (*fn)(void*);
  void* arg;
};

void* thread_main(void* p) {
  ThreadStart start = *static_cast<ThreadStart*>(p);
  delete static_cast<ThreadStart*>(p);
  start.fn(start.arg);
  return nullptr;
}
}

bool start_thread(void (*fn)(void*), void* arg) {
  ThreadStart* start = new ThreadStart{fn, arg};
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&attr, 1024 * 1024);
  pthread_t thread;
  int r = pthread_create(&thread, &attr, thread_main, start);
  pthread_attr_destroy(&attr);
  if (r != 0) {
    delete start;
    return false;
  }
  return true;
}

static_assert(sizeof(pthread_mutex_t) <= 64, "Mutex storage too small");

Mutex::Mutex() { pthread_mutex_init(reinterpret_cast<pthread_mutex_t*>(storage_), nullptr); }
Mutex::~Mutex() { pthread_mutex_destroy(reinterpret_cast<pthread_mutex_t*>(storage_)); }
void Mutex::lock() { pthread_mutex_lock(reinterpret_cast<pthread_mutex_t*>(storage_)); }
void Mutex::unlock() { pthread_mutex_unlock(reinterpret_cast<pthread_mutex_t*>(storage_)); }

double monotonic() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

double wall_time() {
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

void sleep_seconds(double seconds) {
  if (seconds <= 0) return;
  timespec ts;
  ts.tv_sec = static_cast<time_t>(seconds);
  ts.tv_nsec = static_cast<long>((seconds - static_cast<double>(ts.tv_sec)) * 1e9);
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
}

int64_t wall_time_ns() {
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

LocalTime local_now() { return local_from_ns(wall_time_ns()); }

LocalTime local_from_ns(int64_t ns) {
  int64_t secs = ns / 1000000000, rem = ns % 1000000000;
  if (rem < 0) {
    rem += 1000000000;
    secs -= 1;
  }
  time_t t = static_cast<time_t>(secs);
  tm parts;
  localtime_r(&t, &parts);
  return LocalTime{parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec,
                   static_cast<int>(rem / 1000)};
}

bool random_bytes(uint8_t* out, size_t n) {
  while (n > 0) {
    size_t take = n < 256 ? n : 256;
    if (getentropy(out, take) != 0) return false;
    out += take;
    n -= take;
  }
  return true;
}

namespace {
bool write_all_fd(int fd, const void* data, size_t n, Error* err) {
  const char* p = static_cast<const char*>(data);
  while (n > 0) {
    ssize_t w = ::write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      set_error(err, errno);
      return false;
    }
    p += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}
}

bool write_atomic(const std::string& path, const void* data, size_t n, bool private_file, Error* err) {
  std::string tmp = path + ".tmp";
  std::string abs;
  if (!abs_path(path, &abs, err) || !make_dirs(dir_name(abs), err)) return false;
  if (unlink(tmp.c_str()) != 0 && errno != ENOENT) {
    set_error(err, errno);
    return false;
  }
  int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, private_file ? 0600 : 0666);
  if (fd < 0) {
    set_error(err, errno);
    return false;
  }
  bool ok = true;
  if (private_file && fchmod(fd, 0600) != 0) {
    set_error(err, errno);
    ok = false;
  }
  if (ok) ok = write_all_fd(fd, data, n, err);
  if (ok && fsync(fd) != 0) {
    set_error(err, errno);
    ok = false;
  }
  if (::close(fd) != 0 && ok) {
    set_error(err, errno);
    ok = false;
  }
  if (ok && rename(tmp.c_str(), path.c_str()) != 0) {
    set_error(err, errno);
    ok = false;
  }
  if (!ok) unlink(tmp.c_str());
  return ok;
}

bool write_file(const std::string& path, const void* data, size_t n, Error* err) {
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if (fd < 0) {
    set_error(err, errno);
    return false;
  }
  bool ok = write_all_fd(fd, data, n, err);
  if (::close(fd) != 0 && ok) {
    set_error(err, errno);
    ok = false;
  }
  return ok;
}

bool read_file(const std::string& path, std::string* out, Error* err) {
  int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    set_error(err, errno);
    return false;
  }
  out->clear();
  char buf[65536];
  for (;;) {
    ssize_t r = ::read(fd, buf, sizeof(buf));
    if (r < 0) {
      if (errno == EINTR) continue;
      set_error(err, errno);
      ::close(fd);
      return false;
    }
    if (r == 0) break;
    out->append(buf, static_cast<size_t>(r));
  }
  ::close(fd);
  return true;
}

AppendFile::~AppendFile() { close(); }

bool AppendFile::open(const std::string& path, Error* err) {
  close();
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
  if (fd < 0) {
    set_error(err, errno);
    return false;
  }
  handle_ = fd;
  return true;
}

long long AppendFile::size(Error* err) {
  struct stat st;
  if (fstat(static_cast<int>(handle_), &st) != 0) {
    set_error(err, errno);
    return -1;
  }
  return static_cast<long long>(st.st_size);
}

bool AppendFile::write(const void* data, size_t n, Error* err) {
  return write_all_fd(static_cast<int>(handle_), data, n, err);
}

void AppendFile::close() {
  if (handle_ >= 0) ::close(static_cast<int>(handle_));
  handle_ = -1;
}

bool replace_file(const std::string& src, const std::string& dst, Error* err) {
  if (rename(src.c_str(), dst.c_str()) != 0) {
    set_error(err, errno);
    return false;
  }
  return true;
}

bool remove_file(const std::string& path, Error* err) {
  if (unlink(path.c_str()) != 0) {
    set_error(err, errno);
    return false;
  }
  return true;
}

bool path_exists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

bool is_file(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool is_dir(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool list_dir(const std::string& path, std::vector<std::string>* names, Error* err) {
  DIR* d = opendir(path.c_str());
  if (!d) {
    set_error(err, errno);
    return false;
  }
  names->clear();
  while (dirent* e = readdir(d)) {
    std::string name = e->d_name;
    if (name != "." && name != "..") names->push_back(name);
  }
  closedir(d);
  return true;
}

std::string join_path(const std::string& a, const std::string& b) {
  if (!b.empty() && b[0] == '/') return b;
  if (a.empty() || a.back() == '/') return a + b;
  return a + "/" + b;
}

std::string executable_path() {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string raw(size, '\0');
  if (_NSGetExecutablePath(&raw[0], &size) != 0) return "";
  raw.resize(strlen(raw.c_str()));
#else
  std::string raw = "/proc/self/exe";
#endif
  char resolved[PATH_MAX];
  if (!realpath(raw.c_str(), resolved)) return raw;
  return resolved;
}

std::string env_var(const char* name) {
  const char* v = getenv(name);
  return v ? v : "";
}

const char* const kTextNewline = "\n";

namespace {
void write_line(int fd, const std::string& text) {
  std::string line = text + "\n";
  const char* p = line.data();
  size_t n = line.size();
  while (n > 0) {
    ssize_t w = ::write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      return;
    }
    p += w;
    n -= static_cast<size_t>(w);
  }
}
}

namespace {
std::atomic<ConsoleTap> g_tap{nullptr};
}

void print_line(const std::string& text) {
  write_line(1, text);
  if (ConsoleTap tap = g_tap.load()) tap(text, false);
}
void print_error_line(const std::string& text) {
  write_line(2, text);
  if (ConsoleTap tap = g_tap.load()) tap(text, true);
}
void stream_errors_replace() {}
void set_console_tap(ConsoleTap tap) { g_tap.store(tap); }

namespace {
bool over_ssh() { return getenv("SSH_CONNECTION") || getenv("SSH_CLIENT") || getenv("SSH_TTY"); }
}

bool desktop_session() {
  if (over_ssh()) return false;
#if defined(__APPLE__)
  SecuritySessionId id = 0;
  SessionAttributeBits attrs = 0;
  if (SessionGetInfo(callerSecuritySession, &id, &attrs) != errSessionSuccess) return false;
  return (attrs & sessionHasGraphicAccess) != 0;
#else
  const char* x = getenv("DISPLAY");
  const char* w = getenv("WAYLAND_DISPLAY");
  return (x && *x) || (w && *w);
#endif
}

bool open_with_desktop(const std::string& target, Error* err) {
#if defined(__APPLE__)
  const char* program = "open";
#else
  const char* program = "xdg-open";
#endif
  std::string arg0 = program;
  std::string arg1 = target;
  char* argv[] = {&arg0[0], &arg1[0], nullptr};
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
  pid_t pid = 0;
  int rc = posix_spawnp(&pid, program, &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) {
    set_error(err, rc);
    return false;
  }
  start_thread([pid]() {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
  });
  return true;
}

}

#endif
