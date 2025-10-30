#pragma once
#include <glog/logging.h>

#include <atomic>
#include <thread>
#include <vector>

#include "config/proto/robot.pb.h"
#include "robot/comm/serial/serial.h"
#include "robot/perception/interfaces/lidar_interface.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace robot::perception {
class Lds01Driver : public LidarInterface {
 public:
  explicit Lds01Driver(const std::shared_ptr<robot::comm::Serial>& serial,
                       const robot::perception::Lidar& lidar_config);
  ~Lds01Driver() = default;

  absl::Status Init() override;
  std::string GetId() override;
  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override;
  absl::Status Teardown() override;

 private:
  void reading_thread_func();

  std::shared_ptr<robot::comm::Serial> serial_;
  std::string id_;
  mutable robot::perception::PerceptionPacket reusable_packet_;
  std::thread receiving_thread_;
  std::atomic<bool> stop_receiving_;
};
}  // namespace robot::perception
