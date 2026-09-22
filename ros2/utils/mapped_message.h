#pragma once

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "rclcpp/serialized_message.hpp"
#include "ros2/proto/node.pb.h"

namespace ros2_utils {
// Compiles a configured path against installed ROS introspection metadata before
// hardware is opened. Owns type-support libraries for the lifetime of the codec.
class MappedMessage {
 public:
  static absl::StatusOr<std::shared_ptr<MappedMessage>> Create(
      ros2::data_type::Ros2DataType type,
      const ros2::node::ScalarMapping& mapping,
      bool publishing);
  ~MappedMessage();
  const std::string& type_name() const;
  absl::StatusOr<rclcpp::SerializedMessage> Encode(float value) const;
  absl::StatusOr<float> Decode(const rclcpp::SerializedMessage& message) const;

 private:
  struct Impl;
  explicit MappedMessage(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
}  // namespace ros2_utils
