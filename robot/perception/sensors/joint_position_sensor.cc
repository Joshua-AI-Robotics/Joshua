#include "robot/perception/sensors/joint_position_sensor.h"

#include <chrono>
#include <utility>

#include "utils/status_macros.h"

namespace robot::perception {

JointPositionSensor::JointPositionSensor(std::shared_ptr<robot::board::SensorChannel> channel,
                                         const robot::perception::Sensor& sensor_config)
    : channel_(std::move(channel)), id_(sensor_config.sensor_name()) {}

absl::Status JointPositionSensor::Init() {
  // Nothing to do: BoardFactory::GetOrCreate already opened the port and ran
  // the board's IDENTIFY handshake (FEETECH_BUS pings each configured servo)
  // before handing over the channel. Re-reading here could only duplicate
  // that check, and a transient failure would drop the sensor for the whole
  // session — per-read errors surface on the publisher timer instead.
  return absl::OkStatus();
}

absl::Status JointPositionSensor::Teardown() {
  // The board owns the port and is shared with anything else on the same
  // bus, so a sensor must not tear it down. BoardFactory owns board
  // lifetime.
  return absl::OkStatus();
}

std::string JointPositionSensor::GetId() {
  return id_;
}

absl::StatusOr<robot::perception::PerceptionPacket> JointPositionSensor::GetData() {
  ABSL_ASSIGN_OR_RETURN(auto feedback, channel_->ReadFeedback());
  reusable_packet_.Clear();
  reusable_packet_.set_perception_id(id_);
  reusable_packet_.set_timestamp_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
  auto* position = reusable_packet_.mutable_position();
  position->set_position(feedback.position);
  position->set_velocity(feedback.velocity);
  return reusable_packet_;
}

}  // namespace robot::perception
