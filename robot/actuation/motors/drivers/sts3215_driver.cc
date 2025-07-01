#include "robot/actuation/motors/drivers/sts3215_driver.h"



namespace robot::actuation {

namespace {
    constexpr auto kReadAttempt = 50;
}

Sts3215Driver::Sts3215Driver(const std::shared_ptr<robot::comm_interface::Serial>& serial, robot::actuation::Actuator actuator_config):
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
    // 0. Flush the serial port to clear any stale data from previous reads
    serial_->Flush();

    // 1. Create and send the read position packet
    std::vector<uint8_t> packet = create_read_position_packet();
    if (!serial_) {
        LOG(ERROR) << "Serial port interface not initialized.";
        return 0; // Indicate error
    }
    serial_->Write(packet);

    std::vector<uint8_t> response;
    response.reserve(8); // Pre-allocate space

    // Synchronize to 0xFF 0xFF header
    uint8_t byte;
    bool header_found = false;
    for (int attempts = 0; attempts < kReadAttempt; ++attempts) {
        std::vector<uint8_t> first_byte_vec = serial_->Read(1);
        if (first_byte_vec.empty()) {
            LOG(WARNING) << "Serial read timeout during header search (first byte) for servo " << static_cast<int>(servo_id_);
            return 0;
        }
        uint8_t first_byte = first_byte_vec[0];

        if (first_byte == 0xFF) {
            std::vector<uint8_t> second_byte_vec = serial_->Read(1);
            if (second_byte_vec.empty()) {
                LOG(WARNING) << "Serial read timeout during header search (second byte) for servo " << static_cast<int>(servo_id_);
                return 0;
            }
            uint8_t second_byte = second_byte_vec[0];

            if (second_byte == 0xFF) {
                // Header found!
                response.push_back(0xFF);
                response.push_back(0xFF);
                header_found = true;
                break;
            }
            // If first_byte was 0xFF but second_byte was not, we continue the loop to find the next 0xFF.
        }
    }

    if (!header_found) {
        LOG(ERROR) << "Failed to find 0xFF 0xFF header after multiple attempts for servo " << static_cast<int>(servo_id_);
        return 0;
    }

    // Now read the remaining 6 bytes of the packet
    std::vector<uint8_t> remaining_bytes = serial_->Read(6);
    if (remaining_bytes.size() != 6) {
        LOG(ERROR) << "Failed to read remaining 6 bytes after header sync for servo " << static_cast<int>(servo_id_);
        return 0;
    }
    response.insert(response.end(), remaining_bytes.begin(), remaining_bytes.end());

    // 3. Validate response size, start bytes, and error byte
    // The start bytes and total size are now guaranteed by the sync loop.
    if (response[4] != 0) { // Error byte check
        LOG(ERROR) << "Servo " << static_cast<int>(servo_id_) << " returned an error: "
                   << static_cast<int>(response[4]) << ". Raw response: ";
        std::string response_str = "";
        for (size_t i = 0; i < response.size(); ++i) {
            response_str += std::to_string(static_cast<int>(response[i])) + " ";
        }
        LOG(ERROR) << response_str;
        return 0;
    }

    // Validate checksum
    uint8_t calculated_checksum = calculate_checksum(response.begin() + 2, response.begin() + 7);
    uint8_t received_checksum = response[7];
    if (calculated_checksum != received_checksum) {
        LOG(ERROR) << "Checksum mismatch for servo " << static_cast<int>(servo_id_)
                   << ". Calculated: " << static_cast<int>(calculated_checksum)
                   << ", Received: " << static_cast<int>(received_checksum)
                   << ". Raw response: ";
        std::string response_str = "";
        for (size_t i = 0; i < response.size(); ++i) {
            response_str += std::to_string(static_cast<int>(response[i])) + " ";
        }
        LOG(ERROR) << response_str;
        return 0;
    }

    // 4. Extract position bytes safely
    uint16_t position_low_byte = static_cast<uint16_t>(response[5]);
    uint16_t position_high_byte = static_cast<uint16_t>(response[6]);

    uint16_t position = (position_high_byte << 8) | position_low_byte;
    return position;
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

float Sts3215Driver::GetPosition() {
    try{
        return static_cast<float>(read_servo_position());
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to get position.");
    }
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
