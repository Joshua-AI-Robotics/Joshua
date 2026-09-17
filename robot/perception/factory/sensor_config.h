#pragma once

#include "absl/status/status.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "robot/board/proto/board.pb.h"
#include "robot/perception/proto/perception.pb.h"

namespace robot::perception {

// Validates the sensor's measurement type, concrete driver, and board/channel
// references without constructing a driver or opening hardware.
absl::Status ValidateSensorConfig(
    const SinglePerception& sensor,
    const google::protobuf::RepeatedPtrField<robot::board::Board>& boards);

}  // namespace robot::perception
