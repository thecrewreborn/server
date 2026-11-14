#pragma once

#include <cstdint>
#include <string>

#include "core/ordered_map.h"
#include "core/v1_21/relay.h"

namespace tcr {
namespace v1_00 {

constexpr int PORT_OFFSET = 100;
constexpr double DUPLICATE_WINDOW = 0.5;
constexpr size_t CHUNK = 1000;
constexpr uint32_t T_NACK = 0xEF318FDD;
constexpr double RESEND_WINDOW = 0.2;
constexpr size_t TABLE_MAX = 65536;

class Relay final : public v1_21::Relay {
 public:
  Relay(Core& core, v1_21::RelayOptions options, v1_21::RelayHost* host = nullptr);

 protected:
  std::string rpc_name(uint32_t id) const override;
  v1_21::HandleResult dispatch(v1_21::Ctx& ctx, uint32_t id, const Bytes& body) override;
  std::optional<Bytes> seal(const v1_21::Session& sess, const Bytes& plain, Raised* raised) override;
  bool guest_request(uint32_t rpc) const override;
  void say(const v1_21::Addr& addr, const std::string& text, bool always = false) override;
  void probe(const v1_21::Addr& addr, v1_21::DatagramResult* r) override;
  void key_exchange(const v1_21::Addr& addr, v1_21::DatagramResult* r) override;
  void tunnel(v1_21::Session& sess, const Bytes& header, const Bytes& body, const v1_21::Addr& addr,
              v1_21::DatagramResult* r) override;

 private:
  void resend(v1_21::Session& sess, const Bytes& header, const Bytes& body, const v1_21::Addr& addr,
              v1_21::DatagramResult* r);
  static void note(OrderedMap<double>& table, const std::string& key, double now, double window);

  Bytes key_;
  OrderedMap<double> seen_;
  OrderedMap<double> resent_;
};

}
}
