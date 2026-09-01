#pragma once
#include <glog/logging.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "robot/comm/interfaces/stream_transport.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "robot/perception/proto/perception.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace robot::perception {
// SensorType::RANGE_SCAN from an LDS-01. The lidar pushes frames on its own
// schedule, so it takes a StreamTransport rather than a Serial: the parser
// below is the same whether those bytes arrive over UART or, later, UDP,
// which makes the link a comm_type edit in the preset
// (docs/BOARD_LAYER_RFC.md §5.3).
class Lds01Driver : public PerceptionInterface {
 public:
  Lds01Driver(std::shared_ptr<robot::comm::StreamTransport> stream,
              const robot::perception::Sensor& sensor_config);
  ~Lds01Driver() = default;

  absl::Status Init() override;
  std::string GetId() override;
  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override;
  absl::Status Teardown() override;

 private:
  void reading_thread_func();

  std::shared_ptr<robot::comm::StreamTransport> stream_;
  std::string id_;
  mutable robot::perception::PerceptionPacket reusable_packet_;
  std::thread receiving_thread_;
  std::atomic<bool> stop_receiving_;
};
}  // namespace robot::perception
