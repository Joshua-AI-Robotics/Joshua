#include "robot/perception/factory/perception_factory.h"

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "config/proto/robot.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "robot/board/factory/board_factory.h"
#include "robot/board/factory/board_resolver.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/perception/audio/alsa_audio.h"
#include "robot/perception/audio/audio_config.h"
#include "robot/perception/camera/cv_camera.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "robot/perception/lidar/lds01_driver.h"
#include "robot/perception/position/sts3215_position_sensor.h"
#include "utils/status_macros.h"

namespace robot::perception {
namespace {
absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> CreateSts3215PositionSensor(
    const robot::perception::SinglePerception& sensor,
    const google::protobuf::RepeatedPtrField<robot::board::Board>& boards,
    const std::string& owner) {
  const auto& config = sensor.sts3215_encoder_config();
  ABSL_ASSIGN_OR_RETURN(
      auto resolved,
      robot::board::ResolveChannelConfig(boards, owner, config.board_name(), config.channel()));

  ABSL_ASSIGN_OR_RETURN(auto board, robot::board::BoardFactory::GetOrCreate(*resolved.board));
  ABSL_ASSIGN_OR_RETURN(auto channel, board->OpenChannel(config.channel()));
  return std::make_unique<Sts3215PositionSensor>(channel, sensor);
}

}  // namespace

const std::vector<SensorImplementation>& PerceptionFactory::Implementations() {
  static const std::vector<SensorImplementation> entries = {
      {SinglePerception::kSts3215EncoderConfig,
       POSITION,
       "installed",
       [](const SinglePerception& sensor,
          const auto& boards) -> absl::StatusOr<std::unique_ptr<PerceptionInterface>> {
         return CreateSts3215PositionSensor(
             sensor, boards, absl::StrCat("Sensor '", sensor.sensor_name(), "'"));
       },
       "position driver requires POSITION."},
      {SinglePerception::kAlsaAudioConfig,
       AUDIO,
       "installed",
       [](const SinglePerception& sensor,
          const auto&) -> absl::StatusOr<std::unique_ptr<PerceptionInterface>> {
         ABSL_RETURN_IF_ERROR(ValidateAudioConfig(sensor.alsa_audio_config()));
         return std::make_unique<AlsaAudio>(sensor);
       },
       "ALSA driver requires AUDIO sensor_type",
       false},
      {SinglePerception::kOpencvConfig,
       IMAGE,
       "installed",
       [](const SinglePerception& sensor,
          const auto&) -> absl::StatusOr<std::unique_ptr<PerceptionInterface>> {
         return std::make_unique<CvCamera>(sensor);
       },
       "camera driver requires IMAGE."},
      {SinglePerception::kLds01Config,
       RANGE_SCAN,
       "installed",
       [](const SinglePerception& sensor,
          const auto&) -> absl::StatusOr<std::unique_ptr<PerceptionInterface>> {
         if (sensor.lds01_config().comm().transport_type() != robot::comm::BYTE_STREAM)
           return absl::InvalidArgumentError(
               absl::StrCat("Sensor '",
                            sensor.sensor_name(),
                            "': LDS01 requires RANGE_SCAN and BYTE_STREAM comm."));
         ABSL_ASSIGN_OR_RETURN(auto comm,
                               robot::comm::CommFactory::CreateComm(sensor.lds01_config().comm()));
         ABSL_ASSIGN_OR_RETURN(auto stream,
                               robot::comm::GetCommTransport<robot::comm::ByteStream>(comm));
         auto lidar = std::make_unique<Lds01Driver>(stream, sensor);
         ABSL_RETURN_IF_ERROR(lidar->Init());
         return lidar;
       },
       "LDS01 requires RANGE_SCAN and BYTE_STREAM comm."},
      {SinglePerception::SENSOR_CONFIG_NOT_SET,
       IMU_6DOF,
       "unimplemented",
       nullptr,
       "No IMU driver is available."},
      {SinglePerception::SENSOR_CONFIG_NOT_SET,
       FORCE_TORQUE,
       "unimplemented",
       nullptr,
       "No force/torque driver is available."}};
  return entries;
}

absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>>
PerceptionFactory::CreatePerception(
    const robot::perception::SinglePerception& single_perception,
    const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
  const std::string owner = absl::StrCat("Sensor '", single_perception.sensor_name(), "'");
  // Direct factory callers may bypass startup validation. Reject invalid
  // construction requests before resolving resources or opening a transport.
  if (single_perception.sensor_name().empty()) {
    return absl::InvalidArgumentError("Sensor has no sensor_name.");
  }
  if (single_perception.sensor_type() == SENSOR_INVALID) {
    return absl::InvalidArgumentError(absl::StrCat(owner, " has no sensor_type."));
  }

  for (const auto& implementation : Implementations()) {
    if (implementation.config_case != single_perception.sensor_config_case() ||
        !implementation.create)
      continue;
    if (implementation.type != single_perception.sensor_type()) {
      return absl::InvalidArgumentError(
          implementation.include_owner_in_error
              ? absl::StrCat(owner, ": ", implementation.mismatch_message)
              : implementation.mismatch_message);
    }
    return implementation.create(single_perception, boards);
  }
  return absl::InvalidArgumentError(absl::StrCat(owner, " has no sensor_config."));
}

}  // namespace robot::perception
