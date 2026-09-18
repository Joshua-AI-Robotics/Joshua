#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "absl/status/statusor.h"
#include "config/proto/config.pb.h"
#include "google/protobuf/struct.pb.h"
#include "robot/board/interfaces/board_channel.h"

namespace mhs {

// An immutable, validated snapshot. MVP supports one Teensy STEP_DIR actuator.
struct Device {
  robot::action::Actuator actuator;
  robot::board::Board board;
  config::HardwareDevice exposure;
  double steps_per_degree;
};
absl::StatusOr<Device> ResolveDevice(const config::Config& config);

// Hardware-independent policy/lifecycle; channel injection is for C++ tests.
// The executor owns a monitor thread calling Poll even while no client is reading.
class Runtime {
 public:
  using Clock = std::chrono::steady_clock;
  explicit Runtime(Device device);
  ~Runtime();
  // Operator-only entrypoint, never exposed as an MCP tool. Does not enable.
  absl::Status Attach(std::shared_ptr<robot::board::BoardChannel> channel);
  google::protobuf::Struct Handle(const google::protobuf::Struct& request);
  void Poll();

 private:
  absl::Status ReadLocked();
  absl::Status StopLocked(const std::string& outcome);
  google::protobuf::Struct StateLocked() const;
  google::protobuf::Struct DescribeLocked() const;
  Device device_;
  std::shared_ptr<robot::board::BoardChannel> channel_;
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

}  // namespace mhs
