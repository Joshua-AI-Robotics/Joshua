#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/interfaces/sensor_channel.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "robot/perception/proto/perception.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace robot::perception {

// SensorType::JOINT_POSITION over any board channel that can produce it.
//
// Nothing here knows what kind of joint it is reading. The register
// protocol, the port and the bus mutex live in the board; which signal leg
// carries the reading — a Feetech present-position register, a quadrature
// counter, a slot in an EtherCAT input image — lives in the channel. So
// this one class serves every one of them, and a new signal leg needs no
// new sensor class (docs/BOARD_LAYER_RFC.md §5.5, §5.6).
//
// It holds a SensorChannel, not a BoardChannel: a sensor is handed no
// method that can command motion, which matters because perception
// publishers run in their own process.
class JointPositionSensor : public PerceptionInterface {
 public:
  JointPositionSensor(std::shared_ptr<robot::board::SensorChannel> channel,
                      const robot::perception::Sensor& sensor_config);
  ~JointPositionSensor() override = default;

  absl::Status Init() override;
  std::string GetId() override;
  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override;
  absl::Status Teardown() override;

 private:
  std::shared_ptr<robot::board::SensorChannel> channel_;
  std::string id_;
  robot::perception::PerceptionPacket reusable_packet_;
};

}  // namespace robot::perception
