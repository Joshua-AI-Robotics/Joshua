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
#include "robot/board/factory/sensor_channel_validation.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/perception/camera/cv_camera.h"
#include "robot/perception/encoder/sts3215_encoder.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "robot/perception/lidar/lds01_driver.h"
#include "robot/perception/sensors/joint_position_sensor.h"
#include "utils/status_macros.h"

namespace robot::perception {
class PerceptionFactory {
 public:
  // A sensor that declares the new Sensor message resolves through the
  // board layer, exactly as an actuator does: board_name -> Board ->
  // ValidateSensorChannel -> BoardFactory -> OpenChannel. Anything
  // still on the pre-board-layer Camera/Encoder/Lidar messages keeps the
  // old path until it migrates (docs/BOARD_LAYER_RFC.md §10 Phase 6).
  //
  // Callers must pass config.robot().boards() (see ros2/encoder_publisher.cc).
  static absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> CreatePerception(
      const robot::perception::SinglePerception& single_perception,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
    if (single_perception.has_sensor()) {
      return CreateSensor(single_perception.sensor(), boards);
    }
    switch (single_perception.perception_type()) {
      case PerceptionType::CAMERA: {
        const auto& camera = single_perception.camera();
        // Assuming CvCamera for now. Add logic for other camera types if needed.
        return std::make_unique<CvCamera>(camera);
      }
      case PerceptionType::ENCODER: {
        const auto& encoder_config = single_perception.encoder();
        switch (encoder_config.encoder_type()) {
          case EncoderType::STS3215_ENCODER: {
            ABSL_ASSIGN_OR_RETURN(auto serial,
                                  robot::comm::CommFactory::CreateSerial(encoder_config.comm()));
            auto encoder = std::make_unique<Sts3215Encoder>(serial, encoder_config);
            ABSL_RETURN_IF_ERROR(encoder->Init());
            return encoder;
          }
          default:
            return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid encoder type.");
        }
      }

      case PerceptionType::LIDAR: {
        const auto& lidar_config = single_perception.lidar();
        switch (lidar_config.lidar_type()) {
          case LidarType::LDS01: {
            ABSL_ASSIGN_OR_RETURN(
                auto stream,
                robot::comm::CommFactory::CreateStreamTransport(lidar_config.comm()));
            auto lidar = std::make_unique<Lds01Driver>(stream, lidar_config);
            ABSL_RETURN_IF_ERROR(lidar->Init());
            return lidar;
          }
          default:
            return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid lidar type.");
        }
      }
      default:
        return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid perception type.");
    }
  }

  ~PerceptionFactory() = default;
  PerceptionFactory(const PerceptionFactory&) = delete;
  PerceptionFactory& operator=(const PerceptionFactory&) = delete;
  PerceptionFactory(PerceptionFactory&&) = default;
  PerceptionFactory& operator=(PerceptionFactory&&) = default;

 private:
  static absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> CreateSensor(
      const robot::perception::Sensor& sensor,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
    const std::string owner = absl::StrCat("Sensor '", sensor.sensor_name(), "'");

    if (sensor.sensor_type() == robot::perception::SensorType::SENSOR_INVALID) {
      return absl::InvalidArgumentError(absl::StrCat(owner, " has no sensor_type."));
    }

    const bool on_board = !sensor.board_name().empty();
    const bool has_device_config =
        sensor.sensor_config_case() != robot::perception::Sensor::SENSOR_CONFIG_NOT_SET;
    if (on_board && (has_device_config || sensor.has_comm())) {
      return absl::InvalidArgumentError(
          absl::StrCat(owner,
                       " declares both legs: it names a board and also carries its own comm or "
                       "device config. A sensor reaches hardware over exactly one."));
    }
    if (!on_board && !has_device_config) {
      return absl::InvalidArgumentError(
          absl::StrCat(owner,
                       " declares neither leg: set board_name + channel for a sensor on a board, "
                       "or a device config (opencv_config, lds01_config) for a single-stream "
                       "device."));
    }

    if (!on_board) {
      // Single-stream devices still take their pre-Sensor config messages;
      // they migrate with the legacy shape's removal.
      return absl::UnimplementedError(
          absl::StrCat(owner, ": single-stream devices have not moved to Sensor yet."));
    }
    return CreateBoardSensor(sensor, boards, owner);
  }

  // Board leg. Mirrors ActionFactory::CreateBoardActuator step for step,
  // over the same resolver.
  static absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> CreateBoardSensor(
      const robot::perception::Sensor& sensor,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards,
      const std::string& owner) {
    ABSL_ASSIGN_OR_RETURN(
        auto resolved,
        robot::board::ResolveChannelConfig(boards, owner, sensor.board_name(), sensor.channel()));
    ABSL_RETURN_IF_ERROR(
        robot::board::ValidateSensorChannel(sensor.sensor_type(), resolved.channel->signal()));

    ABSL_ASSIGN_OR_RETURN(auto board, robot::board::BoardFactory::GetOrCreate(*resolved.board));
    ABSL_ASSIGN_OR_RETURN(auto channel, board->OpenChannel(sensor.channel()));

    switch (sensor.sensor_type()) {
      case robot::perception::SensorType::JOINT_POSITION:
        return std::make_unique<JointPositionSensor>(channel, sensor);
      default:
        return absl::UnimplementedError(absl::StrCat(
            owner,
            ": sensor_type ",
            robot::perception::SensorType_Name(sensor.sensor_type()),
            " has no board-attached sensor driver yet (docs/BOARD_LAYER_RFC.md §10)."));
    }
  }

  PerceptionFactory() = default;
};
}  // namespace robot::perception
