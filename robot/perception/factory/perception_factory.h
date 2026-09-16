#pragma once

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
#include "robot/perception/encoder/sts3215_encoder.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "robot/perception/lidar/lds01_driver.h"
#include "robot/perception/position/sts3215_position_sensor.h"
#include "utils/status_macros.h"

namespace robot::perception {
class PerceptionFactory {
 public:
  // Callers provide the configured boards used to resolve board-attached
  // sensors.
  static absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> CreatePerception(
      const robot::perception::SinglePerception& single_perception,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
    if (single_perception.sensor_config_case() ==
            robot::perception::SinglePerception::SENSOR_CONFIG_NOT_SET &&
        single_perception.sensor_type() == robot::perception::SensorType::SENSOR_INVALID) {
      return CreateLegacyPerception(single_perception);
    }

    const std::string owner = absl::StrCat("Sensor '", single_perception.sensor_name(), "'");
    if (single_perception.sensor_type() == robot::perception::SensorType::SENSOR_INVALID) {
      return absl::InvalidArgumentError(absl::StrCat(owner, " has no sensor_type."));
    }

    switch (single_perception.sensor_config_case()) {
      case robot::perception::SinglePerception::kSts3215EncoderConfig:
        return CreateSts3215PositionSensor(single_perception, boards, owner);
      case robot::perception::SinglePerception::kOpencvConfig:
      case robot::perception::SinglePerception::kLds01Config:
        return absl::UnimplementedError(
            absl::StrCat(owner, ": direct sensor migration is not implemented yet."));
      case robot::perception::SinglePerception::SENSOR_CONFIG_NOT_SET:
      default:
        return absl::InvalidArgumentError(absl::StrCat(owner, " has no sensor_config."));
    }
  }

  ~PerceptionFactory() = default;
  PerceptionFactory(const PerceptionFactory&) = delete;
  PerceptionFactory& operator=(const PerceptionFactory&) = delete;
  PerceptionFactory(PerceptionFactory&&) = default;
  PerceptionFactory& operator=(PerceptionFactory&&) = default;

 private:
  static absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>>
  CreateLegacyPerception(const robot::perception::SinglePerception& single_perception) {
    switch (single_perception.perception_type()) {
      case PerceptionType::CAMERA: {
        const auto& camera = single_perception.camera();
        return std::make_unique<CvCamera>(camera);
      }
      case PerceptionType::ENCODER: {
        const auto& encoder_config = single_perception.encoder();
        switch (encoder_config.encoder_type()) {
          case EncoderType::STS3215_ENCODER: {
            ABSL_ASSIGN_OR_RETURN(auto comm,
                                  robot::comm::CommFactory::CreateComm(encoder_config.comm()));
            ABSL_ASSIGN_OR_RETURN(
                auto transport, robot::comm::GetCommTransport<robot::comm::MessageTransport>(comm));
            auto encoder = std::make_unique<Sts3215Encoder>(transport, encoder_config);
            ABSL_RETURN_IF_ERROR(encoder->Init());
            return encoder;
          }
          default:
            return absl::InvalidArgumentError("Invalid encoder type.");
        }
      }
      case PerceptionType::LIDAR: {
        const auto& lidar_config = single_perception.lidar();
        switch (lidar_config.lidar_type()) {
          case LidarType::LDS01: {
            ABSL_ASSIGN_OR_RETURN(auto comm,
                                  robot::comm::CommFactory::CreateComm(lidar_config.comm()));
            ABSL_ASSIGN_OR_RETURN(auto stream,
                                  robot::comm::GetCommTransport<robot::comm::ByteStream>(comm));
            auto lidar = std::make_unique<Lds01Driver>(stream, lidar_config);
            ABSL_RETURN_IF_ERROR(lidar->Init());
            return lidar;
          }
          default:
            return absl::InvalidArgumentError("Invalid lidar type.");
        }
      }
      default:
        return absl::InvalidArgumentError("Invalid perception type.");
    }
  }

  static absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>>
  CreateSts3215PositionSensor(const robot::perception::SinglePerception& sensor,
                              const google::protobuf::RepeatedPtrField<robot::board::Board>& boards,
                              const std::string& owner) {
    if (sensor.sensor_type() != robot::perception::SensorType::POSITION) {
      return absl::InvalidArgumentError(
          absl::StrCat(owner, ": sts3215_encoder_config requires POSITION sensor_type."));
    }
    const auto& config = sensor.sts3215_encoder_config();
    ABSL_ASSIGN_OR_RETURN(
        auto resolved,
        robot::board::ResolveChannelConfig(boards, owner, config.board_name(), config.channel()));

    ABSL_ASSIGN_OR_RETURN(auto board, robot::board::BoardFactory::GetOrCreate(*resolved.board));
    ABSL_ASSIGN_OR_RETURN(auto channel, board->OpenChannel(config.channel()));
    return std::make_unique<Sts3215PositionSensor>(channel, sensor);
  }

  PerceptionFactory() = default;
};
}  // namespace robot::perception
