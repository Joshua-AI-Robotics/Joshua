#include "robot/perception/factory/camera_factory.h"
#include "robot/perception/camera/cv_camera.h"

namespace robot::perception {

std::unique_ptr<robot::perception::CameraInterface> CameraFactory::CreateCamera(const robot::perception::Camera& camera_config) {
    // TODO: Add logic to select camera type based on camera_config.
    return std::make_unique<CvCamera>();
}

}  // namespace robot::perception 