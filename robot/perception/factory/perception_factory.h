#pragma once

#include "config/proto/robot.pb.h"
#include "robot/perception/interfaces/perception_interface.h"

#include <memory>
#include <string>

namespace robot::perception{
class PerceptionFactory {
public:
    PerceptionFactory() = default;
    ~PerceptionFactory() = default;

    std::unique_ptr<robot::perception::PerceptionInterface> CreatePerception(const robot::perception::SinglePerception& single_perception);
    
private:
};
}
