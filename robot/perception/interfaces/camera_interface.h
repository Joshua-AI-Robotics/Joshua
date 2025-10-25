#pragma once

#include <memory>
#include <string>

#include "config/proto/robot.pb.h"
#include "robot/perception/interfaces/perception_interface.h"

// Abstract camera interface.
namespace robot::perception {

class CameraInterface : public PerceptionInterface {
 public:
  ~CameraInterface() override = default;
};

}  // namespace robot::perception
