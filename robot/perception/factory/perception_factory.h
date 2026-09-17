#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "config/proto/robot.pb.h"
#include "robot/perception/interfaces/perception_interface.h"

namespace robot::perception {
class PerceptionFactory {
 public:
  // Resolves the concrete sensor config and any board/channel dependencies.
  static absl::StatusOr<std::unique_ptr<PerceptionInterface>> CreatePerception(
      const SinglePerception& config,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards);

 private:
  PerceptionFactory() = default;
};
}  // namespace robot::perception
