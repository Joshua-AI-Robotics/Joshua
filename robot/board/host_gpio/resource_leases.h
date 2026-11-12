#pragma once
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
namespace robot::board {
// Cooperative cross-process pin/mux reservations. Mount the same directory in
// containers. Kernel GPIO requests, PWM exports and bus locks remain separate.
class ResourceLeases {
 public:
  ~ResourceLeases();
  static absl::StatusOr<std::unique_ptr<ResourceLeases>> Acquire(std::vector<std::string> keys,
                                                                 const std::string& directory);

 private:
  std::vector<int> descriptors_;
};
}  // namespace robot::board
