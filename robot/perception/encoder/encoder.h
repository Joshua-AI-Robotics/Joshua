#pragma once

#include "robot/perception/interfaces/encoder_interface.h"

namespace robot::perception {

class Encoder : public EncoderInterface {
public:
    Encoder() = default;
    ~Encoder() override = default;

    void Capture() override;
    std::unique_ptr<robot::nexus::NexusPerceptionPacket> GetData() override;
    std::string GetId() override;
};

} // namespace robot::perception 