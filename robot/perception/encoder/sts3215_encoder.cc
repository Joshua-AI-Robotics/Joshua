#include "robot/perception/encoder/sts3215_encoder.h"
#include <glog/logging.h>
#include "robot/nexus/proto/nexus_packet.pb.h"

namespace robot::perception {

Sts3215Encoder::Sts3215Encoder(const Sts3215EncoderConfig& config) : config_(config) {}

void Sts3215Encoder::Capture() {
    // In a real implementation, this would read from the hardware.
    // For now, it just increments a value.
    current_position_ += 1.0f; // Dummy implementation
    LOG(INFO) << "Sts3215Encoder::Capture() called.";
}

std::unique_ptr<robot::nexus::NexusPerceptionPacket> Sts3215Encoder::GetData() {
    // In a real implementation, this would be populated with actual data
    // from the Capture() step.
    auto packet = std::make_unique<robot::nexus::NexusPerceptionPacket>();
    packet->set_perception_id("sts3215_encoder_0"); // Example ID
    // packet->mutable_encoder_perception()->set_position(current_position_);
    LOG(INFO) << "Sts3215Encoder::GetData() called.";
    return packet;
}

std::string Sts3215Encoder::GetId() {
    return "sts3215_encoder_0"; // Example ID
}

float Sts3215Encoder::GetPosition() {
    return current_position_;
}

} // namespace robot::perception 