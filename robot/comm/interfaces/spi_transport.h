#pragma once
#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
namespace robot::comm {
// One full-duplex 8-bit transaction; chip select is held for the whole transfer.
// Requests and results are bounded to 32 bytes so host and MCU callers agree.
class SpiTransport {
 public:
  virtual ~SpiTransport() = default;
  virtual absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>& bytes) = 0;
};
}  // namespace robot::comm
