#pragma once

#include "robot/perception/interfaces/perception_interface.h"

namespace robot::perception {

class EncoderInterface : public PerceptionInterface {
public:
    ~EncoderInterface() override = default;
};

} // namespace robot::perception 