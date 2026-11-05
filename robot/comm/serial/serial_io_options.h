#pragma once

#include <cstddef>
#include <cstdint>
#include <tuple>

namespace robot::comm {

// Software defaults, not physical timing guarantees. Device policy lives in
// protobuf config; these values also serve direct comm-internal construction.
struct SerialIoOptions {
  uint32_t read_timeout_ms = 500;
  uint32_t read_idle_timeout_ms = 100;
  uint32_t write_timeout_ms = 100;
  uint32_t transaction_timeout_ms = 100;
  size_t max_pending_operations = 16;
  size_t max_pending_bytes = 1048576;
  size_t max_transfer_bytes = 65536;

  auto Values() const {
    return std::tie(read_timeout_ms,
                    read_idle_timeout_ms,
                    write_timeout_ms,
                    transaction_timeout_ms,
                    max_pending_operations,
                    max_pending_bytes,
                    max_transfer_bytes);
  }
  bool operator==(const SerialIoOptions& other) const {
    return Values() == other.Values();
  }
  bool operator!=(const SerialIoOptions& other) const {
    return !(*this == other);
  }
};
}  // namespace robot::comm
