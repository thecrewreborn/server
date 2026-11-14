#include "core/discovery.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/wire.h"

namespace tcr {
namespace discovery {

namespace {

bool put_port(Bytes& out, int64_t port, Raised* raised) {
  if (port < 0) {
    *raised = Raised{"OverflowError", "can't convert negative int to unsigned"};
    return false;
  }
  if (port > 0xFFFF) {
    *raised = Raised{"OverflowError", "int too big to convert"};
    return false;
  }
  wire::put_u16(out, static_cast<uint16_t>(port));
  return true;
}

}

Answer answer(const Bytes& probe, const std::string& peer, const Settings& s, Route& route, Bytes* reply,
              Raised* raised) {
  if (probe.size() < PROBE_SIZE || std::memcmp(probe.data(), PROBE, PROBE_SIZE) != 0) return Answer::kNone;
  std::string ip;
  if (s.advertise && !s.advertise->empty()) {
    ip = *s.advertise;
  } else {
    std::string error;
    if (!route.local_ip_for(peer, &ip, &error)) {
      *raised = Raised{"OSError", error};
      return Answer::kRaised;
    }
  }
  std::optional<std::array<uint8_t, 4>> addr = wire::inet_aton(ip);
  if (!addr) {
    *raised = Raised{"OSError", "illegal IP address string passed to inet_aton"};
    return Answer::kRaised;
  }
  Bytes out(ANSWER, ANSWER + PROBE_SIZE);
  out.insert(out.end(), addr->begin(), addr->end());
  if (!put_port(out, s.relay_port, raised) || !put_port(out, s.http_port, raised)) return Answer::kRaised;
  out.push_back(static_cast<uint8_t>((s.bots ? BOTS : 0) | (s.afk ? AFK : 0) | (s.dlc ? DLC : 0)));
  if (s.https_port || !s.server_id.empty()) {
    if (!put_port(out, s.https_port, raised)) return Answer::kRaised;
  }
  out.insert(out.end(), s.server_id.begin(), s.server_id.end());
  *reply = std::move(out);
  return Answer::kReply;
}

std::string client_text(const Bytes& data) {
  std::vector<uint32_t> cps;
  for (size_t i = PROBE_SIZE; i < data.size(); i++) cps.push_back(data[i] < 0x80 ? data[i] : 0xFFFD);
  size_t a = 0, b = cps.size();
  while (a < b && py::isspace(cps[a])) a++;
  while (b > a && py::isspace(cps[b - 1])) b--;
  std::vector<uint32_t> text(cps.begin() + a, cps.begin() + b);
  if (text.empty()) return "no version";
  if (text.size() > 64) text.resize(64);
  for (uint32_t& c : text) {
    if (!py::isprintable(c)) c = '?';
  }
  return py::from_code_points(text);
}

bool Discovery::may_print(double now) {
  if (now - window_ >= 60) {
    if (left_out_) {
      env_.print_line(stamped(env_, "discovery: " + std::to_string(left_out_) + " line(s) suppressed"));
    }
    window_ = now;
    count_ = 0;
    left_out_ = 0;
  }
  if (count_ >= LINES_PER_MIN) {
    left_out_++;
    return false;
  }
  count_++;
  return true;
}

void Discovery::datagram(const Bytes& data, const std::string& ip, Sender& send) {
  Bytes reply;
  Raised raised;
  if (answer(data, ip, settings_, route_, &reply, &raised) != Answer::kReply) return;
  double now = env_.monotonic();
  if (!replies_.allow(ip, now)) return;
  if (!send.send(reply)) return;
  if (!may_print(now)) return;
  char to[16];
  std::snprintf(to, sizeof(to), "%u.%u.%u.%u", reply[6], reply[7], reply[8], reply[9]);
  env_.print_line(stamped(env_, "discovery " + ip + " (" + client_text(data) + ") -> " + to));
}

}
}
