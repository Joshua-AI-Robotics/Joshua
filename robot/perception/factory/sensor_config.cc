#include "robot/perception/factory/sensor_config.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "robot/board/factory/board_resolver.h"

namespace robot::perception {
absl::Status ValidateSensorConfig(
    const SinglePerception& sensor,
    const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
  const std::string owner = absl::StrCat("Sensor '", sensor.sensor_name(), "'");
  if (sensor.sensor_name().empty()) {
    return absl::InvalidArgumentError("Sensor has no sensor_name.");
  }
  if (sensor.sensor_type() == SENSOR_INVALID) {
    return absl::InvalidArgumentError(absl::StrCat(owner, " has no sensor_type."));
  }
  switch (sensor.sensor_config_case()) {
    case SinglePerception::kSts3215EncoderConfig: {
      if (sensor.sensor_type() != POSITION) {
        return absl::InvalidArgumentError(
            absl::StrCat(owner, ": sts3215_encoder_config requires POSITION sensor_type."));
      }
      const auto& config = sensor.sts3215_encoder_config();
      return robot::board::ResolveChannelConfig(
                 boards, owner, config.board_name(), config.channel())
          .status();
    }
    case SinglePerception::kOpencvConfig:
      if (sensor.sensor_type() != IMAGE) {
        return absl::InvalidArgumentError(
            absl::StrCat(owner, ": opencv_config requires IMAGE sensor_type."));
      }
      return absl::OkStatus();
    case SinglePerception::kLds01Config:
      if (sensor.sensor_type() != RANGE_SCAN) {
        return absl::InvalidArgumentError(
            absl::StrCat(owner, ": lds01_config requires RANGE_SCAN sensor_type."));
      }
      if (!sensor.lds01_config().has_comm() ||
          sensor.lds01_config().comm().transport_type() != robot::comm::BYTE_STREAM) {
        return absl::InvalidArgumentError(
            absl::StrCat(owner, ": lds01_config requires BYTE_STREAM comm."));
      }
      return absl::OkStatus();
    case SinglePerception::SENSOR_CONFIG_NOT_SET:
    default:
      return absl::InvalidArgumentError(absl::StrCat(owner, " has no sensor_config."));
  }
}
}  // namespace robot::perception
