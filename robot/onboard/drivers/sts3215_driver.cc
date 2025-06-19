#include "robot/onboard/drivers/sts3215_driver.h"

Sts3215Driver::Sts3215Driver(const std::shared_ptr<Serial>& serial, int servo_id):
    serial_(serial), servo_id_(servo_id)
    {
        move_time_in_ms_ = 40;
        move_speed_ = 1500;
        LOG(INFO) << "Sts3215Driver initialized";
    }


Sts3215Driver::~Sts3215Driver() {
    LOG(INFO) << "Sts3215Driver destroyed";
}

uint8_t Sts3215Driver::calculate_checksum(const std::vector<uint8_t>& data) {
    uint8_t checksum = 0;
    for (uint8_t byte : data) {
        checksum += byte;
    }
    return ~checksum & 0xFF;
}

std::vector<uint8_t> Sts3215Driver::create_move_packet(uint16_t position, uint16_t speed) {
    std::vector<uint8_t> packet = {
        (uint8_t)0xFF, (uint8_t)0xFF, servo_id_, (uint8_t)0x09, (uint8_t)0x03, (uint8_t)0x2A,
        (uint8_t)(position & 0xFF), (uint8_t)((position >> 8) & 0xFF),
        (uint8_t)((uint8_t)move_time_in_ms_ & 0xFF), (uint8_t)(((uint8_t)move_time_in_ms_ >> 8) & 0xFF),
        (uint8_t)(speed & 0xFF), (uint8_t)((speed >> 8) & 0xFF)
    };
    
    packet.push_back(calculate_checksum(std::vector<uint8_t>(packet.begin() + 2, packet.end())));
    return packet;
}

std::vector<uint8_t> Sts3215Driver::create_torque_packet(uint8_t enable) {
    std::vector<uint8_t> packet = {
        0xFF, 0xFF, servo_id_, 0x04, 0x03, 0x28, enable
    };
    packet.push_back(calculate_checksum(std::vector<uint8_t>(packet.begin() + 2, packet.end())));
    return packet;
}

std::vector<uint8_t> Sts3215Driver::create_read_position_packet() {
    std::vector<uint8_t> packet = {
        (uint8_t)0xFF, (uint8_t)0xFF, servo_id_, (uint8_t)0x04, (uint8_t)0x02, (uint8_t)0x38, (uint8_t)0x02
    };
    packet.push_back(calculate_checksum(std::vector<uint8_t>(packet.begin() + 2, packet.end())));
    return packet;
}

uint16_t Sts3215Driver::read_servo_position() {
    std::vector<uint8_t> packet = create_read_position_packet();
    serial_->Write(packet);
    std::string response = serial_->Read();
    if (response.size() < 6) {
        LOG(ERROR) << "Invalid response from servo " << static_cast<int>(servo_id_);
        return 0;
    }
    uint16_t position = (response[4] << 8) | response[5];
    return position;
}

void Sts3215Driver::SetSpeed(float value) {
    move_speed_ = value;
}

void Sts3215Driver::SetPosition(float angle) {
    try{
        serial_->Write(create_move_packet(uint16_t(angle), uint16_t(move_speed_)));
    } catch (const std::exception& e) {  
        LOG(ERROR) << "Error: " << e.what();
        throw std::runtime_error("Failed to set position.");
    }
}

void Sts3215Driver::SetTorque(float torque) {
    try{
        serial_->Write(create_torque_packet(uint16_t(torque)));
    } catch (const std::exception& e) {  
        LOG(ERROR) << "Error: " << e.what();
        throw std::runtime_error("Failed to set torque.");
    }
}