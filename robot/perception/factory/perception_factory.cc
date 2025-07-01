#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/camera/cv_camera.h"
#include "robot/perception/encoder/sts3215_encoder.h"
#include "robot/comm_interface/factory/comm_factory.h"

namespace robot::perception {

std::unique_ptr<robot::perception::PerceptionInterface> PerceptionFactory::CreatePerception(const robot::perception::Sensor& sensor_config) {
    switch (sensor_config.sensor_type()) {
        case SensorType::CAMERA:
            // Assuming CvCamera for now. Add logic for other camera types if needed.
            return std::make_unique<CvCamera>();
        case SensorType::ENCODER:
            if (sensor_config.has_sts3215_encoder_config()) {
                auto serial = robot::comm_interface::CommFactory::GetInstance().GetSerial(sensor_config.serial_config());
                return std::make_unique<Sts3215Encoder>(serial,sensor_config);
            }
            return nullptr;
        default:
            return nullptr;
    }
}

}  // namespace robot::perception 