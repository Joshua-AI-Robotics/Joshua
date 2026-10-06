// Diagnostic workflow for the serial smoke CLI, separate from runtime code.
// Preflight is hardware-free; Run uses the production host session over the
// supplied transport. Tests supply an in-memory firmware endpoint.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>

#include "absl/status/status.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/interfaces/message_transport.h"

namespace robot::board::diagnostics {
struct SerialValidationOptions {
  std::string mode = "handshake";  // handshake, configure, exercise
  int channel = -1;
  std::optional<float> target_steps;
  bool allow_enable = false;
  int sessions = 1;
  int settle_ms = 2000;  // CLI boot-settle allowance, before the first reset.
};

// Call before opening a port. Configuration modes use only the selected channel.
absl::Status ValidateSerialOptions(const Board& board, const SerialValidationOptions& options);

// Each session resets, checks identity, optionally exercises a channel, and
// attempts ESTOP on exit. Repeated sessions do not physically reopen the port.
// Cancellation is checked between exchanges; cleanup still attempts ESTOP.
absl::Status RunSerialValidation(const Board& board,
                                 const SerialValidationOptions& options,
                                 std::shared_ptr<robot::comm::MessageTransport> transport,
                                 std::ostream& output,
                                 std::function<bool()> cancelled = {});
}  // namespace robot::board::diagnostics
