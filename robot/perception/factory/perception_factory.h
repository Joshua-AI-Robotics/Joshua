#pragma once

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "config/proto/robot.pb.h"
#include "robot/perception/interfaces/perception_interface.h"

namespace robot::perception {
struct SensorImplementation {
  SinglePerception::SensorConfigCase config_case;
  SensorType type;
  const char* support_scope;
  absl::StatusOr<std::unique_ptr<PerceptionInterface>> (*create)(
      const SinglePerception&, const google::protobuf::RepeatedPtrField<robot::board::Board>&);
  const char* mismatch_message;
  bool include_owner_in_error = true;
};
class PerceptionFactory {
 public:
  static const std::vector<SensorImplementation>& Implementations();
  // Resolves the concrete sensor config and any board/channel dependencies.
  static absl::StatusOr<std::unique_ptr<PerceptionInterface>> CreatePerception(
      const SinglePerception& config,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards);

 private:
  PerceptionFactory() = default;
};
}  // namespace robot::perception
