// Public capability factory. Concrete serial/SOEM classes stay in the .cc.
// The EthercatTransport alternative is the retained TI-demo API, not the new
// correlated cyclic capability; production v2 EtherCAT adapters remain pending.
#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/interfaces/byte_stream.h"
#include "robot/comm/interfaces/correlated_cyclic_transport.h"
#include "robot/comm/interfaces/message_transport.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::comm {

struct PairedTransports {
  std::shared_ptr<MessageTransport> message;
  std::shared_ptr<CorrelatedCyclicTransport> cyclic;
  absl::Duration response_timeout = absl::ZeroDuration();
};

using CommTransport = std::variant<std::shared_ptr<ByteStream>,
                                   std::shared_ptr<MessageTransport>,
                                   std::shared_ptr<CorrelatedCyclicTransport>,
                                   PairedTransports,
                                   std::shared_ptr<robot::comm::ethercat::EthercatTransport>>;

template <typename Transport>
absl::StatusOr<std::shared_ptr<Transport>> GetCommTransport(const CommTransport& transport) {
  if (const auto* pair = std::get_if<PairedTransports>(&transport)) {
    if constexpr (std::is_same_v<Transport, MessageTransport>) {
      if (pair->message) return pair->message;
    }
    if constexpr (std::is_same_v<Transport, CorrelatedCyclicTransport>) {
      if (pair->cyclic) return pair->cyclic;
    }
  }
  const auto* selected = std::get_if<std::shared_ptr<Transport>>(&transport);
  if (selected == nullptr || !*selected) {
    return absl::InvalidArgumentError("Configured comm does not provide the requested transport.");
  }
  return *selected;
}

class CommFactory {
 public:
  static absl::StatusOr<CommTransport> CreateComm(const robot::comm::Comm& config);

  // Replaces the result of CreateComm without changing the consumer call
  // path or opening hardware. Pass nullptr to restore production behavior.
  // For tests.
  static void SetCommTransportFactoryForTesting(
      std::function<absl::StatusOr<CommTransport>(const robot::comm::Comm&)> factory);

  // Returns a cached instance per interface name — an EtherCAT NIC has
  // exactly one master, and two ecx_init()s on one NIC fight over the raw
  // socket. Later calls return the same transport and fail if they request a
  // different process-data mode.
  static absl::StatusOr<std::shared_ptr<robot::comm::ethercat::EthercatTransport>> CreateEthercat(
      const robot::comm::EthercatConfig& config);

  // Replaces the SOEM transport constructor so cache semantics are testable
  // without a NIC. Pass nullptr to restore the default. For tests.
  static void SetEthercatTransportFactoryForTesting(
      std::function<std::shared_ptr<robot::comm::ethercat::EthercatTransport>()> factory);

  // Tears down and forgets every cached EtherCAT transport. For tests.
  static void ResetEthercatTransportCacheForTesting();

  ~CommFactory() = default;
  CommFactory(const CommFactory&) = delete;
  CommFactory& operator=(const CommFactory&) = delete;
  CommFactory(CommFactory&&) = default;
  CommFactory& operator=(CommFactory&&) = default;

 private:
  CommFactory() = default;
};
}  // namespace robot::comm
