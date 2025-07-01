#pragma once

#include "robot/perception/interfaces/encoder_interface.h"
#include "robot/perception/proto/perception.pb.h"
#include "robot/comm_interface/serial/serial.h"

namespace robot::perception {

class Sts3215Encoder : public EncoderInterface {
public:
    explicit Sts3215Encoder(const std::shared_ptr<robot::comm_interface::Serial>& serial, const robot::perception::Sensor& sensor_config);
    ~Sts3215Encoder() override = default;

    std::unique_ptr<robot::nexus::NexusPerceptionPacket> GetData() override;
    std::string GetId() override;
    float GetPosition() override;

private:
    std::shared_ptr<robot::comm_interface::Serial> serial_;
    std::string id_;
    uint8_t servo_id_;
    uint8_t calculate_checksum(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end);
    std::vector<uint8_t> create_read_position_packet();
    uint16_t read_servo_position();    
};

} // namespace robot::perception 