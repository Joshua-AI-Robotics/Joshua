#pragma once

#include <set>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::comm {

using TransportSet = std::set<TransportType>;

// Returns the capabilities `comm` requires, from either required_transports or
// the single-capability transport_type. INVALID_ARGUMENT if neither or both is
// set, or if required_transports names an invalid or duplicate capability.
absl::StatusOr<TransportSet> RequiredTransports(const Comm& comm);

// INVALID_ARGUMENT, naming `owner`, unless `comm` requires exactly `expected`.
absl::Status ExpectRequiredTransports(const Comm& comm,
                                      const TransportSet& expected,
                                      std::string_view owner);

std::string TransportSetName(const TransportSet& transports);

}  // namespace robot::comm
