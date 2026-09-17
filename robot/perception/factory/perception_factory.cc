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

  switch (single_perception.sensor_config_case()) {
    case robot::perception::SinglePerception::kSts3215EncoderConfig:
      if (single_perception.sensor_type() != POSITION) {
        return absl::InvalidArgumentError(
            absl::StrCat(owner, ": position driver requires POSITION."));
      }
      return CreateSts3215PositionSensor(single_perception, boards, owner);
    case robot::perception::SinglePerception::kOpencvConfig:
      if (single_perception.sensor_type() != IMAGE) {
        return absl::InvalidArgumentError(absl::StrCat(owner, ": camera driver requires IMAGE."));
      }
      // Preserve lazy camera acquisition: GetData opens the configured camera.
      return std::make_unique<CvCamera>(single_perception);
    case robot::perception::SinglePerception::kLds01Config: {
      if (single_perception.sensor_type() != RANGE_SCAN ||
          single_perception.lds01_config().comm().transport_type() != robot::comm::BYTE_STREAM) {
        return absl::InvalidArgumentError(
            absl::StrCat(owner, ": LDS01 requires RANGE_SCAN and BYTE_STREAM comm."));
      }
      ABSL_ASSIGN_OR_RETURN(
          auto comm, robot::comm::CommFactory::CreateComm(single_perception.lds01_config().comm()));
      ABSL_ASSIGN_OR_RETURN(auto stream,
                            robot::comm::GetCommTransport<robot::comm::ByteStream>(comm));
      auto lidar = std::make_unique<Lds01Driver>(stream, single_perception);
      ABSL_RETURN_IF_ERROR(lidar->Init());
      return lidar;
    }
    case robot::perception::SinglePerception::SENSOR_CONFIG_NOT_SET:
    default:
      return absl::InvalidArgumentError(absl::StrCat(owner, " has no sensor_config."));
  }
}

}  // namespace robot::perception
