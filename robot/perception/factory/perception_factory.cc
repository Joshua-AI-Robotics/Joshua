#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/camera/cv_camera.h"
#include "robot/perception/encoder/sts3215_encoder.h"
#include "robot/comm_interface/factory/comm_factory.h"

namespace robot::perception {

std::unique_ptr<robot::perception::PerceptionInterface> PerceptionFactory::CreatePerception(const robot::perception::SinglePerception& single_perception) {
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
            if (encoder.comm_type() == robot::comm_interface::CommType::SERIAL) {
                // TODO: Serial should not be hardcoded.
                auto serial = robot::comm_interface::CommFactory::GetInstance().GetSerial(encoder.serial_config());
                return std::make_unique<Sts3215Encoder>(serial, encoder);
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

}  // namespace robot::perception 