#include "robot/perception/position/sts3215_position_sensor.h"

#include <chrono>
#include <utility>

#include "utils/status_macros.h"

namespace robot::perception {

Sts3215PositionSensor::Sts3215PositionSensor(std::shared_ptr<robot::board::BoardChannel> channel,
                                             const robot::perception::SinglePerception& config)
    : channel_(std::move(channel)), id_(config.sensor_name()) {}

absl::Status Sts3215PositionSensor::Init() {
  // FeetechBusBoard initializes and identifies the configured servo before
  // handing its channel to this driver.
  return absl::OkStatus();
}

absl::Status Sts3215PositionSensor::Teardown() {
  // The board owns the port and is shared with anything else on the same
  // bus, so a sensor must not tear it down. BoardFactory owns board
  // lifetime.
  return absl::OkStatus();
}

std::string Sts3215PositionSensor::GetId() {
  return id_;
}

absl::StatusOr<robot::perception::PerceptionPacket> Sts3215PositionSensor::GetData() {
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
