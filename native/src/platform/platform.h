#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace tcr::plat {

struct Error {
  int code = 0;
  std::string text;
  bool winerror = false;
};

enum class ErrorKind { kOther, kTimeout, kReset, kBrokenPipe, kAborted, kRefused, kAddrInUse, kAccess, kNotFound };
ErrorKind error_kind(const Error& e);
const char* python_error_name(const Error& e);
std::string python_error_text(const Error& e);
bool is_addr_in_use(const Error& e);
Error system_error(int code);

bool init();

using Socket = intptr_t;
constexpr Socket kNoSocket = -1;

struct Addr {
  std::string ip;
  int port = 0;
};

Socket tcp_listen(const char* ip, int port, int backlog, bool reuse, Error* err);
Socket tcp_accept(Socket listener, double seconds, Addr* peer, Error* err);
Addr local_addr(Socket s);

int wait_readable(Socket s, double seconds, Error* err);
int wait_writable(Socket s, double seconds, Error* err);

constexpr long kWouldBlock = -2;
long sock_recv(Socket s, void* buf, size_t n, Error* err);
long sock_send(Socket s, const void* buf, size_t n, Error* err);

void shutdown_write(Socket s);
void close_socket(Socket s);

Socket udp_open(const char* ip, int port, bool reuse, bool broadcast, Error* err);
void udp_report_connreset(Socket s);
bool udp_port_free(int port);
long udp_recvfrom(Socket s, void* buf, size_t n, Addr* from, Error* err);
long udp_sendto(Socket s, const void* buf, size_t n, const Addr& to, Error* err);

bool route_ip(const std::string& peer, int port, std::string* ip, Error* err);
bool resolve_ipv4(const std::string& host, int port, std::string* ip, std::string* error);
bool bind_address(const std::string& host, std::string* ip, std::string* error_line);

bool start_thread(void (*fn)(void*), void* arg);

template <typename F>
bool start_thread(F&& f) {
  using Fn = typename std::decay<F>::type;
  Fn* heap = new Fn(static_cast<F&&>(f));
  bool ok = start_thread([](void* p) {
    Fn* fn = static_cast<Fn*>(p);
    (*fn)();
    delete fn;
  }, heap);
  if (!ok) delete heap;
  return ok;
}

class Mutex {
 public:
  Mutex();
  ~Mutex();
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
  void lock();
  void unlock();

 private:
  alignas(16) unsigned char storage_[64];
};

class Lock {
 public:
  explicit Lock(Mutex& m) : m_(m) { m_.lock(); }
  ~Lock() { m_.unlock(); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;

 private:
  Mutex& m_;
};

double monotonic();
double wall_time();
void sleep_seconds(double seconds);

struct LocalTime {
  int year, month, day, hour, minute, second, microsecond;
};
int64_t wall_time_ns();
LocalTime local_now();
LocalTime local_from_ns(int64_t ns);

bool random_bytes(uint8_t* out, size_t n);

bool write_atomic(const std::string& path, const void* data, size_t n, bool private_file, Error* err);
bool write_file(const std::string& path, const void* data, size_t n, Error* err);
bool read_file(const std::string& path, std::string* out, Error* err);

class AppendFile {
 public:
  AppendFile() = default;
  ~AppendFile();
  AppendFile(const AppendFile&) = delete;
  AppendFile& operator=(const AppendFile&) = delete;
  bool open(const std::string& path, Error* err);
  long long size(Error* err);
  bool write(const void* data, size_t n, Error* err);
  void close();

 private:
  intptr_t handle_ = -1;
};

bool replace_file(const std::string& src, const std::string& dst, Error* err);
bool remove_file(const std::string& path, Error* err);
bool path_exists(const std::string& path);
bool is_file(const std::string& path);
bool is_dir(const std::string& path);
bool list_dir(const std::string& path, std::vector<std::string>* names, Error* err);
bool abs_path(const std::string& path, std::string* out, Error* err);
bool make_dirs(const std::string& path, Error* err);
std::string dir_name(const std::string& path);
std::string join_path(const std::string& a, const std::string& b);
std::string executable_path();
std::string env_var(const char* name);

extern const char* const kTextNewline;

void catch_interrupt();
bool interrupted();

void print_line(const std::string& text);
void print_error_line(const std::string& text);
void stream_errors_replace();
using ConsoleTap = void (*)(const std::string& text, bool error);
void set_console_tap(ConsoleTap tap);

bool desktop_session();
bool open_with_desktop(const std::string& target, Error* err);

}
