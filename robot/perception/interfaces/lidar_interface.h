#pragma once

#include <memory>
#include <string>

#include "config/proto/robot.pb.h"
#include "robot/perception/interfaces/perception_interface.h"

// Abstract lidar interface.
namespace robot::perception {

class LidarInterface : public PerceptionInterface {
 public:
  ~LidarInterface() override = default;
};

}  // namespace robot::perception
