#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/interfaces/byte_stream.h"
#include "robot/comm/interfaces/correlated_cyclic_transport.h"
#include "robot/comm/interfaces/message_transport.h"

namespace robot::comm {

// The capabilities one configured comm provides. A single link may provide
// several at once, such as EtherCAT mailbox and correlated PDO exchange from
// one master; absent capabilities are null.
struct CommCapabilities {
  std::shared_ptr<ByteStream> byte_stream;
  std::shared_ptr<MessageTransport> message;
  std::shared_ptr<CorrelatedCyclicTransport> correlated_cyclic;
  // Raw process-image access, used only by the retained TI EtherCAT demo.
  std::shared_ptr<ethercat::EthercatTransport> process_image;
};

// A consumer's handle on a configured comm. The capabilities share the
// underlying link with every other lease on it.
class CommLease {
 public:
  CommLease() = default;
  explicit CommLease(CommCapabilities capabilities) : capabilities_(std::move(capabilities)) {}

  const CommCapabilities& capabilities() const {
    return capabilities_;
  }

  // Returns the requested capability, or INVALID_ARGUMENT if this comm does
  // not provide it.
  template <typename Transport>
  absl::StatusOr<std::shared_ptr<Transport>> Require() const {
    std::shared_ptr<Transport> selected;
    if constexpr (std::is_same_v<Transport, ByteStream>) {
      selected = capabilities_.byte_stream;
    } else if constexpr (std::is_same_v<Transport, MessageTransport>) {
      selected = capabilities_.message;
    } else if constexpr (std::is_same_v<Transport, CorrelatedCyclicTransport>) {
      selected = capabilities_.correlated_cyclic;
    } else if constexpr (std::is_same_v<Transport, ethercat::EthercatTransport>) {
      selected = capabilities_.process_image;
    } else {
      static_assert(!std::is_same_v<Transport, Transport>, "Not a comm capability type.");
    }
    if (selected == nullptr) {
      return absl::InvalidArgumentError(
          "Configured comm does not provide the requested transport.");
    }
    return selected;
  }

 private:
  CommCapabilities capabilities_;
};

}  // namespace robot::comm
