#pragma once

#include "robot/perception/interfaces/perception_interface.h"

// Abstract encoder interface.
namespace robot::perception {

class EncoderInterface : public PerceptionInterface {
public:
    ~EncoderInterface() override = default;
    virtual float GetPosition() = 0;
};

} // namespace robot::perception 