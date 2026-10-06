#pragma once

#include <functional>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/interfaces/comm_lease.h"
#include "robot/comm/interfaces/message_framer.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::comm {

// Protocol facts a consumer supplies when acquiring a comm.
struct CommOptions {
  // Delimits responses when MESSAGE is required over a link without message
  // boundaries, such as serial.
  std::shared_ptr<const MessageFramer> message_framer;
};

class CommFactory {
 public:
  // Validates that `config`'s mechanism provides every required transport,
  // opens or reuses the link, and returns a lease exposing exactly those
  // capabilities.
  static absl::StatusOr<CommLease> Acquire(const robot::comm::Comm& config,
                                           const CommOptions& options = {});

  // Replaces how Acquire opens the link, after config validation, without
  // changing the consumer call path or opening hardware. The returned lease
  // must still provide every required transport. Pass nullptr to restore
  // production behavior. For tests.
  static void SetCommLeaseFactoryForTesting(
      std::function<absl::StatusOr<CommLease>(const robot::comm::Comm&, const CommOptions&)>
          factory);

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

  CommFactory() = delete;
};
}  // namespace robot::comm
