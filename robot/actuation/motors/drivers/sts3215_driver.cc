#include "robot/actuation/motors/drivers/sts3215_driver.h"



namespace robot::actuation {

namespace {
    constexpr auto kReadAttempt = 50;
}

Sts3215Driver::Sts3215Driver(const std::shared_ptr<robot::comm_interface::Serial>& serial, const robot::actuation::Actuator& actuator_config):
    serial_(serial)
    {   
        auto sts_config = actuator_config.sts3215_config();
        servo_id_ = sts_config.id();
        move_speed_ = sts_config.move_speed();
        move_time_in_ms_ = sts_config.move_time_in_ms();
        physical_lower_limit_ = sts_config.physical_lower_limit();
        physical_upper_limit_ = sts_config.physical_upper_limit();
        operational_lower_limit_ = sts_config.operational_lower_limit();
        operational_upper_limit_ = sts_config.operational_upper_limit();
        idle_position_ = sts_config.idle_position();

        LOG(INFO) << "Sts3215Driver Servo ID: " << static_cast<int>(servo_id_)<< " initialized";
    }


Sts3215Driver::~Sts3215Driver() {
    LOG(INFO) << "Sts3215Driver Servo ID: " << static_cast<int>(servo_id_)<< " destroyed.";
}

uint8_t Sts3215Driver::calculate_checksum(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end) {
    uint8_t checksum = 0;
    for (auto it = begin; it != end; ++it) {
        checksum += *it;
    }
    return ~checksum & 0xFF;
}

std::vector<uint8_t> Sts3215Driver::create_move_packet(uint16_t position) {
    uint16_t integer_move_time_ms = static_cast<uint16_t>(move_time_in_ms_); // Assume move time is always positive.
    std::vector<uint8_t> packet = {
        static_cast<uint8_t>(0xFF),
        static_cast<uint8_t>(0xFF),
        servo_id_,
        static_cast<uint8_t>(0x09),
        static_cast<uint8_t>(0x03),
        static_cast<uint8_t>(0x2A),

        // Goal Position (16-bit)
        static_cast<uint8_t>(position & 0xFF),         // Goal Position Low Byte
        static_cast<uint8_t>((position >> 8) & 0xFF),  // Goal Position High Byte

        // Moving Time (16-bit) - Assuming move_time_in_ms_ is a 16-bit value
        static_cast<uint8_t>(integer_move_time_ms & 0xFF),         // Moving Time Low Byte
        static_cast<uint8_t>((integer_move_time_ms >> 8) & 0xFF),  // Moving Time High Byte

        // Moving Speed (16-bit)
        static_cast<uint8_t>(move_speed_ & 0xFF),         // Moving Speed Low Byte
        static_cast<uint8_t>((move_speed_ >> 8) & 0xFF)   // Moving Speed High Byte
    };

    packet.push_back(calculate_checksum(packet.begin() + 2, packet.end()));
    return packet;
}

std::vector<uint8_t> Sts3215Driver::create_torque_packet(uint8_t enable) {
    std::vector<uint8_t> packet = {
        static_cast<uint8_t>(0xFF), // Start Byte 1
        static_cast<uint8_t>(0xFF), // Start Byte 2
        servo_id_,
        static_cast<uint8_t>(0x04), // Length (4 bytes follow: Instr, Addr, Enable)
        static_cast<uint8_t>(0x03), // Instruction: WRITE_DATA
        static_cast<uint8_t>(0x28), // Parameter 1: Address (TORQUE_ENABLE = 40)
        enable                      // Parameter 2: Enable/Disable (0x00 or 0x01)
    };
    packet.push_back(calculate_checksum(packet.begin() + 2, packet.end()));
    return packet;
}

void Sts3215Driver::SetSpeed(float value) {
    move_speed_ = static_cast<uint16_t>(value);
}

void Sts3215Driver::SetPosition(float angle) {
    try{
        serial_->Write(create_move_packet(static_cast<uint16_t>(angle)));
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        throw std::runtime_error("Failed to set position.");
    }
}

void Sts3215Driver::SetTorque(float torque) {
    try{
        serial_->Write(create_torque_packet(static_cast<uint16_t>(torque)));
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        throw std::runtime_error("Failed to set torque.");
    }
}

void Sts3215Driver::SetAction(std::unique_ptr<robot::nexus::NexusActionPacket> action_packet) {
    if (!action_packet) {
        return;
    }
    
    try {
        // Avoid default value.
        if(!action_packet->sts3215_action().move_time_in_ms() == 0){
            move_time_in_ms_ = action_packet->sts3215_action().move_time_in_ms();
        }
        if(!action_packet->sts3215_action().move_speed() == 0){
            move_speed_ = action_packet->sts3215_action().move_speed();
        }
        if(!action_packet->sts3215_action().position() == 0){
            serial_->Write(create_move_packet(action_packet->sts3215_action().position()));
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        throw std::runtime_error("Failed to set action.");
    }
}

std::string Sts3215Driver::GetId() {
    return std::to_string(servo_id_);
}

void Sts3215Driver::SetMiddlePosition(){
    try{
        auto middle_position = (operational_lower_limit_ + operational_upper_limit_)/2;
        serial_->Write(create_move_packet(middle_position));
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        throw std::runtime_error("Failed to set middle position.");
    }
}

void Sts3215Driver::SetIdlePosition(){
    try{
        serial_->Write(create_move_packet(idle_position_));
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        throw std::runtime_error("Failed to set idle position.");
    }
}

void Sts3215Driver::GracefulShutdown(){
    try{
        move_speed_ = 1000;
        SetIdlePosition();
        sleep(2);
        SetTorque(0);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        throw std::runtime_error("Failed to graceful shutdown.");
    }
}

} // namespace robot::actuation
