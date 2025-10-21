#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/camera/cv_camera.h"
#include "robot/perception/encoder/sts3215_encoder.h"
#include "robot/comm/factory/comm_factory.h"

namespace robot::perception {

absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> PerceptionFactory::CreatePerception(const robot::perception::SinglePerception& single_perception) {
    switch (single_perception.perception_type()) {
        case PerceptionType::CAMERA:
        {
            const auto& camera = single_perception.camera();
            // Assuming CvCamera for now. Add logic for other camera types if needed.
            return std::make_unique<CvCamera>(camera);
        }
        case PerceptionType::ENCODER:
        {
            const auto& encoder = single_perception.encoder();
            auto serial = robot::comm::CommFactory::CreateSerial(encoder.comm());
            if(!serial.ok()) {
                return serial.status();
            }
            return std::make_unique<Sts3215Encoder>(serial.value(), encoder);
        }
        default:
            return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid perception type.");
    }
}

}  // namespace robot::perception 