#pragma once

#include "robot/perception/interfaces/encoder_interface.h"
#include "robot/perception/proto/perception.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "robot/comm_interface/serial/serial.h"
#include <any>
#include <optional>

namespace robot::perception {

class Sts3215Encoder : public EncoderInterface {
public:
    explicit Sts3215Encoder(const std::shared_ptr<robot::comm_interface::Serial>& serial, const robot::perception::Encoder& encoder_config);
    ~Sts3215Encoder() override = default;

    robot::perception::PerceptionPacket GetData() override;
    std::string GetId() override;
    float GetPosition() override;

private:
    std::shared_ptr<robot::comm_interface::Serial> serial_;
    std::string id_;
    uint8_t servo_id_;
    mutable robot::perception::PerceptionPacket reusable_packet_;  // Pre-allocated packet for reuse
    
    uint8_t calculate_checksum(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end);
    std::vector<uint8_t> create_read_position_packet();
    std::optional<uint16_t> read_servo_position();    
};

} // namespace robot::perception 