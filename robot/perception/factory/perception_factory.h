#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/proto/robot.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "robot/board/factory/board_factory.h"
#include "robot/board/factory/sensor_channel_validation.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/perception/camera/cv_camera.h"
#include "robot/perception/encoder/board_encoder.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "robot/perception/lidar/lds01_driver.h"
#include "utils/status_macros.h"

namespace robot::perception {
class PerceptionFactory {
 public:
  // Board-layer factory, the perception twin of ActionFactory::CreateAction
  // (docs/BOARD_LAYER_RFC.md §6.5): an encoder resolves board_name against
  // `boards`, validates encoder_type against the channel's drive, and reads
  // through BoardChannel. Callers must pass config.robot().boards() (see
  // ros2/encoder_publisher.cc).
  //
  // Cameras and lidars still construct their own comm: neither is a
  // board-layer device yet, since no board exposes a frame or scan channel.
  // They keep the pre-board-layer path until BoardChannel grows a
  // non-motor feedback shape.
  static absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> CreatePerception(
      const robot::perception::SinglePerception& single_perception,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
    switch (single_perception.perception_type()) {
      case PerceptionType::CAMERA: {
        const auto& camera = single_perception.camera();
        // Assuming CvCamera for now. Add logic for other camera types if needed.
        return std::make_unique<CvCamera>(camera);
      }
      case PerceptionType::ENCODER:
        return CreateBoardEncoder(single_perception.encoder(), boards);

      case PerceptionType::LIDAR: {
        const auto& lidar_config = single_perception.lidar();
        switch (lidar_config.lidar_type()) {
          case LidarType::LDS01: {
            ABSL_ASSIGN_OR_RETURN(auto serial,
                                  robot::comm::CommFactory::CreateSerial(lidar_config.comm()));
            auto lidar = std::make_unique<Lds01Driver>(serial, lidar_config);
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
  // Resolution flow, mirroring ActionFactory::CreateBoardActuator:
  // board_name -> Board config -> ValidateSensorChannel -> BoardFactory ->
  // OpenChannel -> encoder.
  static absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> CreateBoardEncoder(
      const robot::perception::Encoder& encoder,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
    if (encoder.encoder_type() == robot::perception::EncoderType::ENCODER_INVLAID) {
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "Encoder '" + encoder.encoder_name() + "' has no encoder_type.");
    }
    if (encoder.has_comm()) {
      return absl::Status(
          absl::StatusCode::kInvalidArgument,
          "Encoder '" + encoder.encoder_name() +
              "' sets deprecated comm; encoders no longer own a port. Set board_name + "
              "channel and declare the port once in boards{} "
              "(docs/BOARD_LAYER_RFC.md §6.3).");
    }
    if (encoder.board_name().empty()) {
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "Encoder '" + encoder.encoder_name() + "' has no board_name.");
    }

    const robot::board::Board* board_config = nullptr;
    for (const auto& board : boards) {
      if (board.name() == encoder.board_name()) {
        board_config = &board;
        break;
      }
    }
    if (board_config == nullptr) {
      return absl::Status(absl::StatusCode::kNotFound,
                          "Encoder '" + encoder.encoder_name() + "' references board '" +
                              encoder.board_name() + "' but no boards{} entry declares that name.");
    }

    const robot::board::Channel* channel_config = nullptr;
    for (const auto& channel : board_config->channels()) {
      if (channel.index() == encoder.channel()) {
        channel_config = &channel;
        break;
      }
    }
    if (channel_config == nullptr) {
      return absl::Status(absl::StatusCode::kNotFound,
                          "Encoder '" + encoder.encoder_name() + "' uses channel " +
                              std::to_string(encoder.channel()) + " but board '" +
                              board_config->name() + "' does not declare it.");
    }
    ABSL_RETURN_IF_ERROR(
        robot::board::ValidateSensorChannel(encoder.encoder_type(), channel_config->drive()));

    ABSL_ASSIGN_OR_RETURN(auto board, robot::board::BoardFactory::GetOrCreate(*board_config));
    ABSL_ASSIGN_OR_RETURN(auto channel, board->OpenChannel(encoder.channel()));
    auto board_encoder = std::make_unique<BoardEncoder>(channel, encoder);
    ABSL_RETURN_IF_ERROR(board_encoder->Init());
    return board_encoder;
  }

  PerceptionFactory() = default;
};
}  // namespace robot::perception
