#include "robot/perception/encoder/encoder.h"
#include <glog/logging.h>

namespace robot::perception {

void Encoder::Capture() {
    // In a real implementation, this would read from the hardware.
    // For now, it does nothing as GetData will return a dummy value.
    LOG(INFO) << "Encoder::Capture() called.";
}

std::unique_ptr<robot::nexus::NexusPerceptionPacket> Encoder::GetData() {
    // In a real implementation, this would be populated with actual data
    // from the Capture() step.
    auto packet = std::make_unique<robot::nexus::NexusPerceptionPacket>();
    packet->set_perception_id("encoder_0"); // Example ID
    // packet->mutable_encoder_perception()->set_...
    LOG(INFO) << "Encoder::GetData() called.";
    return packet;
}

std::string Encoder::GetId() {
    return "encoder_0"; // Example ID
}

} // namespace robot::perception 