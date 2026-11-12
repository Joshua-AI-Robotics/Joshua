#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
namespace robot::comm {
// Configured 7-bit target. A nonempty write followed by a read uses one repeated-
// START transaction. Combined request/response is bounded to 32 bytes.
class I2cTransport {
 public:
  virtual ~I2cTransport() = default;
  virtual absl::StatusOr<std::vector<uint8_t>> Transfer(const std::vector<uint8_t>& write,
                                                        size_t read) = 0;
};
}  // namespace robot::comm
