#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "absl/status/statusor.h"
#include "config/proto/config.pb.h"
#include "google/protobuf/struct.pb.h"
#include "robot/action/interfaces/action_interface.h"

namespace ros2_actuator {

// An immutable, validated snapshot. MVP supports one Teensy STEP_DIR actuator.
struct Device {
  robot::action::Actuator actuator;
  robot::board::Board board;
  config::HardwareDevice exposure;
  double steps_per_degree;
  ros2::node::Node node;
  std::string command_topic;
  std::string status_topic;
};
absl::StatusOr<Device> ResolveDevice(const config::Config& config);

// Bounded move policy shared by ROS command producers. Drivers own hardware I/O.
// The actuator node calls Poll even while no client is reading.
class ActuatorSession {
 public:
  using Clock = std::chrono::steady_clock;
  explicit ActuatorSession(Device device);
  ~ActuatorSession();
  // Operator-only entrypoint, never exposed as an MCP tool. Does not enable.
  absl::Status Attach(std::shared_ptr<robot::action::ActionInterface> action);
  google::protobuf::Struct Handle(const google::protobuf::Struct& request);
  void Poll();

 private:
  absl::Status ReadLocked();
  absl::Status StopLocked(const std::string& outcome);
  google::protobuf::Struct StateLocked() const;
  google::protobuf::Struct DescribeLocked() const;
  Device device_;
  std::shared_ptr<robot::action::ActionInterface> action_;
  mutable std::mutex mutex_;
  bool reference_valid_ = false;
  bool feedback_valid_ = false;
  bool disable_acknowledged_ = false;
  double steps_ = 0;
  double target_ = 0;
  uint64_t command_id_ = 0;
  std::string outcome_ = "offline";
  std::string error_;
  Clock::time_point received_{};
  Clock::time_point deadline_{};
  int64_t received_unix_ms_ = 0;
};

}  // namespace ros2_actuator
