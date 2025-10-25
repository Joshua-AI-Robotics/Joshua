#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/proto/robot.pb.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/perception/camera/cv_camera.h"
#include "robot/perception/encoder/sts3215_encoder.h"
#include "robot/perception/lidar/lds01_driver.h"
#include "robot/perception/interfaces/perception_interface.h"

namespace robot::perception {
class PerceptionFactory {
 public:
  static absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> CreatePerception(
      const robot::perception::SinglePerception& single_perception) {
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
            auto serial = robot::comm::CommFactory::CreateSerial(encoder_config.comm());
            if (!serial.ok()) {
              return serial.status();
            }
            auto encoder = std::make_unique<Sts3215Encoder>(serial.value(), encoder_config);
            if (!encoder->Init().ok()) {
              return absl::Status(absl::StatusCode::kInternal, "Failed to init encoder.");
            }
            return encoder;
          }
          default:
            return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid encoder type.");
        }
      }

            case PerceptionType::LIDAR:
            {
                const auto& lidar_config = single_perception.lidar();
                switch (lidar_config.lidar_type()){
                    case LidarType::LDS01:
                    {
                        auto serial = robot::comm::CommFactory::CreateSerial(lidar_config.comm());
                        if(!serial.ok()) {
                            return serial.status();
                        }
                        auto lidar = std::make_unique<Lds01Driver>(serial.value(), lidar_config);
                        if(!lidar->Init().ok()){
                            return absl::Status(absl::StatusCode::kInternal, "Failed to init lidar.");
                        }
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
  PerceptionFactory() = default;
};
}  // namespace robot::perception
