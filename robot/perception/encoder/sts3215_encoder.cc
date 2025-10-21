#include "robot/perception/encoder/sts3215_encoder.h"
#include <glog/logging.h>
#include <chrono>

namespace robot::perception {

namespace {
    constexpr auto kReadAttempt = 50;
}

Sts3215Encoder::Sts3215Encoder(const std::shared_ptr<robot::comm::Serial>& serial, const robot::perception::Encoder& encoder_config)
    : serial_(serial) {
    servo_id_ = encoder_config.sts3215_encoder_config().servo_id();
    id_ = GetId();
}

std::string Sts3215Encoder::GetId() {
    auto id = "sts3215_encoder_" + std::to_string(servo_id_);
    return id;
}

absl::StatusOr<robot::perception::PerceptionPacket> Sts3215Encoder::GetData() {
    auto position_opt = read_servo_position();
    if (position_opt) {
        reusable_packet_.Clear();
        reusable_packet_.set_perception_id(id_);
        reusable_packet_.set_timestamp_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        reusable_packet_.mutable_position()->set_position(static_cast<float>(*position_opt));
        return reusable_packet_;
    }
    
    // Return empty packet on failure
    reusable_packet_.Clear();
    return absl::Status(absl::StatusCode::kInternal, "Failed to read servo position");
}

absl::StatusOr<float> Sts3215Encoder::GetPosition() {
    auto position_opt = read_servo_position();
    if (position_opt) {
        return static_cast<float>(*position_opt);
    }
    return absl::Status(absl::StatusCode::kInternal, "Failed to read servo position");
}

uint8_t Sts3215Encoder::calculate_checksum(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end) {
    uint8_t checksum = 0;
    for (auto it = begin; it != end; ++it) {
        checksum += *it;
    }
    return ~checksum & 0xFF;
}

std::vector<uint8_t> Sts3215Encoder::create_read_position_packet() {
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

std::optional<uint16_t> Sts3215Encoder::read_servo_position() {
    // 0. Flush the serial port to clear any stale data from previous reads
    if(!serial_->Flush().ok()) {
        LOG(ERROR) << "Failed to flush serial port.";
        return std::nullopt;
    }

    // 1. Create and send the read position packet
    std::vector<uint8_t> packet = create_read_position_packet();
    if (!serial_) {
        LOG(ERROR) << "Serial port interface not initialized.";
        return std::nullopt; // Indicate error
    }
    if(!serial_->Write(packet).ok()) {
        LOG(ERROR) << "Failed to write to serial port.";
        return std::nullopt;
    }

    std::vector<uint8_t> response;
    response.reserve(8); // Pre-allocate space

    // Synchronize to 0xFF 0xFF header
    bool header_found = false;
    for (int attempts = 0; attempts < kReadAttempt; ++attempts) {
        absl::StatusOr<std::vector<uint8_t>> first_byte_vec = serial_->Read(1);
        if (!first_byte_vec.ok() || first_byte_vec.value().empty()) {
            LOG(WARNING) << "Serial read timeout during header search (first byte) for servo " << static_cast<int>(servo_id_);
            return std::nullopt;
        }
        uint8_t first_byte = first_byte_vec.value()[0];

        if (first_byte == 0xFF) {
            absl::StatusOr<std::vector<uint8_t>> second_byte_vec = serial_->Read(1);
            if (!second_byte_vec.ok() || second_byte_vec.value().empty()) {
                LOG(WARNING) << "Serial read timeout during header search (second byte) for servo " << static_cast<int>(servo_id_);
                return std::nullopt;
            }
            uint8_t second_byte = second_byte_vec.value()[0];

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
        return std::nullopt;
    }

    // Now read the remaining 6 bytes of the packet
    absl::StatusOr<std::vector<uint8_t>> remaining_bytes = serial_->Read(6);
    if (!remaining_bytes.ok() || remaining_bytes.value().size() != 6) {
        LOG(ERROR) << "Failed to read remaining 6 bytes after header sync for servo " << static_cast<int>(servo_id_);
        return std::nullopt;
    }
    response.insert(response.end(), remaining_bytes.value().begin(), remaining_bytes.value().end());

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
        return std::nullopt;
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
        return std::nullopt;
    }

    // 4. Extract position bytes safely
    uint16_t position_low_byte = static_cast<uint16_t>(response[5]);
    uint16_t position_high_byte = static_cast<uint16_t>(response[6]);

    uint16_t position = (position_high_byte << 8) | position_low_byte;
    return position;
}

} // namespace robot::perception 