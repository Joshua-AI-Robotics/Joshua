#pragma once

#include "robot/config/robot.pb.h"
#include "robot/perception/interfaces/camera_interface.h"

#include <memory>
#include <string>

namespace robot::perception{
class CameraFactory {
public:
    CameraFactory() = default;
    ~CameraFactory() = default;

    std::unique_ptr<robot::perception::CameraInterface> CreateCamera();
    
private:
};
}
