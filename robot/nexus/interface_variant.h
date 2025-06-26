#pragma once
#include <variant>
#include <memory>
#include "robot/actuation/interfaces/motor_interface.h"
#include "robot/perception/interfaces/camera_interface.h"

namespace robot::nexus {
    using ActionInterface = std::variant<
        std::shared_ptr<robot::actuation::MotorInterface>
        // Add other interface types as needed
    >;
    using PerceptionInterface = std::variant<
        std::shared_ptr<robot::perception::CameraInterface>
        // Add other perception types as needed
    >;
}