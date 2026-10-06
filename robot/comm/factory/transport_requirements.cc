#include "robot/comm/factory/transport_requirements.h"

#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace robot::comm {

absl::StatusOr<TransportSet> RequiredTransports(const Comm& comm) {
  const bool has_single = comm.transport_type() != TransportType::TRANSPORT_INVALID;
  if (has_single && comm.required_transports_size() > 0) {
    return absl::InvalidArgumentError(
        "Comm sets both transport_type and required_transports; set only one.");
  }
  if (has_single) {
    return TransportSet{comm.transport_type()};
  }
  if (comm.required_transports_size() == 0) {
    return absl::InvalidArgumentError("Comm has an invalid transport_type.");
  }
  TransportSet transports;
  for (int i = 0; i < comm.required_transports_size(); ++i) {
    const auto transport = comm.required_transports(i);
    if (transport == TransportType::TRANSPORT_INVALID || !TransportType_IsValid(transport)) {
      return absl::InvalidArgumentError("Comm required_transports names an invalid transport.");
    }
    if (!transports.insert(transport).second) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Comm required_transports lists ", TransportType_Name(transport), " more than once."));
    }
  }
  return transports;
}

absl::Status ExpectRequiredTransports(const Comm& comm,
                                      const TransportSet& expected,
                                      std::string_view owner) {
  auto required = RequiredTransports(comm);
  if (!required.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(owner, ": ", required.status().message()));
  }
  if (*required != expected) {
    return absl::InvalidArgumentError(absl::StrCat(owner,
                                                   " requires ",
                                                   TransportSetName(expected),
                                                   " transport, but its comm requires ",
                                                   TransportSetName(*required),
                                                   "."));
  }
  return absl::OkStatus();
}

std::string TransportSetName(const TransportSet& transports) {
  std::vector<std::string> names;
  names.reserve(transports.size());
  for (const auto transport : transports) {
    names.push_back(TransportType_Name(transport));
  }
  return absl::StrJoin(names, "+");
}

}  // namespace robot::comm
