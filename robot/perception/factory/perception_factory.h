#pragma once

#include "config/proto/robot.pb.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include <memory>
#include <string>
#include "robot/perception/camera/cv_camera.h"
#include "robot/perception/encoder/sts3215_encoder.h"
#include "robot/comm/factory/comm_factory.h"

namespace robot::perception{
class PerceptionFactory {
public:
    PerceptionFactory() = default;
    ~PerceptionFactory() = default;

    static absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> CreatePerception(const robot::perception::SinglePerception& single_perception){
        switch (single_perception.perception_type()) {
            case PerceptionType::CAMERA:
            {
                const auto& camera = single_perception.camera();
                // Assuming CvCamera for now. Add logic for other camera types if needed.
                return std::make_unique<CvCamera>(camera);
            }
            case PerceptionType::ENCODER:
            {
                const auto& encoder_config = single_perception.encoder();
                switch (encoder_config.encoder_type()) {
                    case EncoderType::STS3215_ENCODER:
                    {
                        auto serial = robot::comm::CommFactory::CreateSerial(encoder_config.comm());
                        if(!serial.ok()) {
                            return serial.status();
                        }
                        auto encoder = std::make_unique<Sts3215Encoder>(serial.value(), encoder_config);
                        if(!encoder->Init().ok()) {
                            return absl::Status(absl::StatusCode::kInternal, "Failed to init encoder.");
                        }
                        return encoder;
                    }
                    default:
                        return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid encoder type.");
                }
            }
            default:
                return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid perception type.");
        }
    }
    
private:
};
}
