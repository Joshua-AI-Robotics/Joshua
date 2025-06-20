#include "robot/onboard/drivers/sts3215_driver.h"

namespace robot::onboard {

Sts3215Driver::Sts3215Driver(const std::shared_ptr<robot::comm_interface::Serial>& serial, robot_config::Motor motor_config):
    serial_(serial),
    servo_id_(motor_config.sts3215_config().id())
    {
        move_time_in_ms_ = 40;
        move_speed_ = motor_config.sts3215_config().move_speed();
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

std::vector<uint8_t> Sts3215Driver::create_move_packet(uint16_t position, uint16_t speed) {
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
        static_cast<uint8_t>(speed & 0xFF),         // Moving Speed Low Byte
        static_cast<uint8_t>((speed >> 8) & 0xFF)   // Moving Speed High Byte
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

std::vector<uint8_t> Sts3215Driver::create_read_position_packet() {
    std::vector<uint8_t> packet = {
        static_cast<uint8_t>(0xFF), // Start Byte 1
        static_cast<uint8_t>(0xFF), // Start Byte 2
        servo_id_,
        static_cast<uint8_t>(0x04), // Length (4 bytes follow: Instr, Addr, DataLength)
        static_cast<uint8_t>(0x02), // Instruction: READ_DATA
        static_cast<uint8_t>(0x38), // Parameter 1: Starting Address (PRESENT_POSITION_L = 56)
        static_cast<uint8_t>(0x02)  // Parameter 2: Length of Data to Read (2 bytes for position)
    };
    packet.push_back(calculate_checksum(packet.begin() + 2, packet.end()));
    return packet;
}

uint16_t Sts3215Driver::read_servo_position() {
    // 1. Create and send the read position packet
    std::vector<uint8_t> packet = create_read_position_packet();
    if (!serial_) {
        LOG(ERROR) << "Serial port interface not initialized.";
        return 0; // Indicate error
    }
    serial_->Write(packet);

    std::string response = serial_->Read();

    // 3. Validate response size
    // Typical Dynamixel status packet for 2 data bytes (position) is 8 bytes:
    // 0xFF 0xFF ID LENGTH ERROR P1 P2 CHECKSUM
    // So, expecting a minimum of 8 bytes for a full response.
    // Your check for < 6 is very lenient; consider making it < 8 or appropriate.
    if (response.size() < 6) { // Original check, maintaining for consistency
        LOG(ERROR) << "Invalid/short response from servo " << static_cast<int>(servo_id_)
                   << ". Received " << response.size() << " bytes.";
        return 0; // Return 0 to indicate error or invalid reading
    }

    // 4. Extract position bytes safely
    // The crucial part: cast char to uint8_t, then to uint16_t before shifting.
    // This prevents sign extension if char is signed and the byte value is > 127.
    uint16_t position_low_byte = static_cast<uint16_t>(static_cast<uint8_t>(response[4]));
    uint16_t position_high_byte = static_cast<uint16_t>(static_cast<uint8_t>(response[5]));

    uint16_t position = (position_high_byte << 8) | position_low_byte;
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

} // namespace robot::onboard
