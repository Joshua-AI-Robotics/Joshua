#pragma once
#include "absl/status/status.h"
#include "ros2/proto/node.pb.h"

namespace ros2_utils {
std::string RosMessageType(ros2::data_type::Ros2DataType type);
std::string NumericFieldPath(ros2::data_type::Ros2DataType type,
                             const ros2::node::ScalarMapping& mapping);
absl::Status ValidateNumericMapping(ros2::data_type::Ros2DataType type,
                                    const ros2::node::ScalarMapping& mapping,
                                    bool publishing);
}  // namespace ros2_utils
