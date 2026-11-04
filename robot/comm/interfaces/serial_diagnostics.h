// Read-only value snapshots; no descriptor, worker or concrete transport leaks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace robot::comm {
enum class SerialLinkState { kReady, kFaulted, kClosed };
struct SerialDiagnostics {
  std::string configured_port;
  std::string canonical_port;
  uint64_t generation = 0;
  SerialLinkState state = SerialLinkState::kClosed;
  size_t pending_operations = 0;
  size_t pending_bytes = 0;
  uint64_t admitted = 0;
  uint64_t completed = 0;
  uint64_t rejected = 0;
  uint64_t timed_out = 0;
  uint64_t cancelled = 0;
  uint64_t bytes_written = 0;
  uint64_t bytes_read = 0;
  uint64_t last_queue_wait_us = 0;
  uint64_t max_queue_wait_us = 0;
  uint64_t last_operation_us = 0;
  uint64_t max_operation_us = 0;
  std::string last_error;  // At most 512 bytes; informational, not a result code.
};
}  // namespace robot::comm
