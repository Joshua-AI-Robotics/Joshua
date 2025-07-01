#pragma once
#include <variant>
#include <memory>
#include "robot/actuation/interfaces/actuation_interface.h"
#include "robot/perception/interfaces/camera_interface.h"

namespace robot::nexus {
    using ActionInterface = std::variant<
        std::unique_ptr<robot::actuation::ActuationInterface>
        // Add other interface types as needed
    >;
    using PerceptionInterface = std::variant<
        std::unique_ptr<robot::perception::CameraInterface>
        // Add other perception types as needed
    >;
}