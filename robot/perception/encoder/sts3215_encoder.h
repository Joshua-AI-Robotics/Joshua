#pragma once

#include "robot/perception/interfaces/encoder_interface.h"
#include "robot/perception/proto/perception.pb.h"

namespace robot::perception {

class Sts3215Encoder : public EncoderInterface {
public:
    explicit Sts3215Encoder(const Sts3215EncoderConfig& config);
    ~Sts3215Encoder() override = default;

    void Capture() override;
    std::unique_ptr<robot::nexus::NexusPerceptionPacket> GetData() override;
    std::string GetId() override;
    float GetPosition() override;
private:
    Sts3215EncoderConfig config_;
    float current_position_ = 0.0f;
};

} // namespace robot::perception 