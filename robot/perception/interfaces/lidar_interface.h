#pragma once

#include "config/proto/robot.pb.h"
#include "robot/perception/interfaces/perception_interface.h"

#include <memory>
#include <string>

// Abstract lidar interface.
namespace robot::perception {

class LidarInterface : public PerceptionInterface {
public:
    ~LidarInterface() override = default;
};

} // namespace robot::perception 
