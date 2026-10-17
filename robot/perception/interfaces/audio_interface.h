#pragma once
#include "robot/perception/interfaces/perception_interface.h"
namespace robot::perception {
class AudioInterface : public PerceptionInterface {
 public:
  ~AudioInterface() override = default;
};
}  // namespace robot::perception
