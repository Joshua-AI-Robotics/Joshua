#pragma once

#include "robot/config/robot.pb.h"
#include "robot/nexus/proto/nexus_packet.pb.h"
#include "robot/perception/interfaces/perception_interface.h"

#include <memory>
#include <string>

namespace robot::perception {

class CameraInterface : public PerceptionInterface {
public:
    ~CameraInterface() override = default;
};

} // namespace robot::perception 