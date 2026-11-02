// Public capability factory. Concrete serial/SOEM classes stay in the .cc.
// Paired message/cyclic endpoints share one resource lease; bus I/O stays private.
#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/interfaces/byte_stream.h"
#include "robot/comm/interfaces/correlated_cyclic_transport.h"
#include "robot/comm/interfaces/message_transport.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::comm {

namespace ethercat {
class EthercatMasterIo;
}

struct PairedTransports {
  std::shared_ptr<MessageTransport> message;
  std::shared_ptr<CorrelatedCyclicTransport> cyclic;
  absl::Duration response_timeout = absl::ZeroDuration();
};

using CommTransport = std::variant<std::shared_ptr<ByteStream>,
                                   std::shared_ptr<MessageTransport>,
                                   std::shared_ptr<CorrelatedCyclicTransport>,
                                   PairedTransports>;

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
  // Serial connections share one port and must agree on baud rate and timing.
  static absl::StatusOr<CommTransport> CreateComm(const robot::comm::Comm& config);
  // Pure validation, safe before opening hardware and usable by config checks.
  static absl::Status ValidatePairedEthercatConfig(const robot::comm::EthercatConfig& config);
  static absl::Status ValidateSerialConfig(const robot::comm::SerialConfig& config);

  // Replaces the result of CreateComm without changing the consumer call
  // path or opening hardware. Pass nullptr to restore production behavior.
  // For tests.
  static void SetCommTransportFactoryForTesting(
      std::function<absl::StatusOr<CommTransport>(const robot::comm::Comm&)> factory);

  // Forgets cached serial connections. Release all consumers before calling.
  // For tests.
  static void ResetSerialTransportCacheForTesting();

  // Comm-internal backend injection. The abstract type is forward-declared so
  // no owner-worker/concrete implementation headers escape this public seam.
  static void SetEthercatMasterIoFactoryForTesting(
      std::function<std::unique_ptr<ethercat::EthercatMasterIo>()> factory);

  // Stops cached owners; entries remain reserved until their last lease is
  // released. For tests.
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
