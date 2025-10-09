#include "robot/action/motors/drivers/sts3215_driver.h"



namespace robot::action {

namespace {
    constexpr auto kReadAttempt = 50;
}

Sts3215Driver::Sts3215Driver(const std::shared_ptr<robot::comm_interface::Serial>& serial, const robot::action::Actuator& action_config):
    serial_(serial)
    {   
        auto sts_config = action_config.sts3215_config();
        servo_id_ = sts_config.servo_id();
        move_speed_ = sts_config.move_speed();
        move_time_in_ms_ = sts_config.move_time_in_ms();
        physical_lower_limit_ = action_config.physical_lower_limit();
        physical_upper_limit_ = action_config.physical_upper_limit();
        operational_lower_limit_ = action_config.operational_lower_limit();
        operational_upper_limit_ = action_config.operational_upper_limit();
        idle_position_ = sts_config.idle_position();
        id_ = GetId();
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

absl::Status Sts3215Driver::SetAction(const robot::action::ActionPacket& action_packet) {
    try {
        LOG(INFO) << "Executing action [ID: " << action_packet.action_id() 
                  << ", Timestamp: " << action_packet.timestamp_ms() << "]";
        
        switch(action_packet.action_type_case()) {
            case robot::action::ActionPacket::kPreset:
                switch(action_packet.preset()){
                    case robot::action::PresetCommand::PRESET_MIDDLE_POSITION:
                        LOG(INFO) << "Executing preset: MIDDLE_POSITION";
                        if(!SetMiddlePosition().ok()) {
                            LOG(ERROR) << "Failed to set middle position";
                            return absl::Status(absl::StatusCode::kInternal, "Failed to set middle position");
                        }
                        break;
                    case robot::action::PresetCommand::PRESET_IDLE_POSITION:
                        LOG(INFO) << "Executing preset: IDLE_POSITION";
                        if(!SetIdlePosition().ok()) {
                            LOG(ERROR) << "Failed to set idle position";
                            return absl::Status(absl::StatusCode::kInternal, "Failed to set idle position");
                        }
                        break;
                    case robot::action::PresetCommand::PRESET_GRACEFUL_SHUTDOWN:
                        LOG(INFO) << "Executing preset: GRACEFUL_SHUTDOWN";
                        if(!GracefulShutdown().ok()) {
                            LOG(ERROR) << "Failed to graceful shutdown";
                            return absl::Status(absl::StatusCode::kInternal, "Failed to graceful shutdown");
                        }
                        break;
                    case robot::action::PresetCommand::PRESET_ENABLE_TORQUE:
                        LOG(INFO) << "Executing preset: ENABLE_TORQUE";
                        if(!SetTorque(1.0f).ok()) {
                            LOG(ERROR) << "Failed to set torque";
                            return absl::Status(absl::StatusCode::kInternal, "Failed to set torque");
                        }
                        break;
                    case robot::action::PresetCommand::PRESET_DISABLE_TORQUE:
                        LOG(INFO) << "Executing preset: DISABLE_TORQUE";
                        if(!SetTorque(0.0f).ok()) {
                            LOG(ERROR) << "Failed to set torque";
                            return absl::Status(absl::StatusCode::kInternal, "Failed to set torque");
                        }
                        break;
                    default:
                        LOG(WARNING) << "Unknown preset command: " << action_packet.preset();
                        break;
                }
                break;

            case robot::action::ActionPacket::kComplex: {
                const auto& complex_action = action_packet.complex();
                
                // Validate at least one field is set
                if (!complex_action.has_position() && !complex_action.has_speed() && 
                    !complex_action.has_torque()) {
                    LOG(WARNING) << "Complex action has no fields set";
                    break;
                }
                
                LOG(INFO) << "Executing complex action";
                
                // Apply changes in order: speed first, then torque, then position
                if(complex_action.has_speed()) {
                    if (complex_action.speed() >= 0) {
                        if(!SetSpeed(complex_action.speed()).ok()) {
                            LOG(ERROR) << "Failed to set speed";
                            return absl::Status(absl::StatusCode::kInternal, "Failed to set speed");
                        }
                        LOG(INFO) << "Set speed: " << complex_action.speed();
                    } else {
                        LOG(WARNING) << "Invalid speed value: " << complex_action.speed();
                    }
                }
                
                if(complex_action.has_torque()) {
                    if (complex_action.torque() >= 0.0f) {
                        if(!SetTorque(complex_action.torque()).ok()) {
                            LOG(ERROR) << "Failed to set torque";
                            return absl::Status(absl::StatusCode::kInternal, "Failed to set torque");
                        }
                        LOG(INFO) << "Set torque: " << complex_action.torque();
                    } else {
                        LOG(WARNING) << "Invalid torque value: " << complex_action.torque();
                    }
                }
                
                if(complex_action.has_position()) {
                    float pos = complex_action.position();
                    if (pos >= operational_lower_limit_ && pos <= operational_upper_limit_) {
                        if(!SetPosition(pos).ok()) {
                            LOG(ERROR) << "Failed to set position";
                            return absl::Status(absl::StatusCode::kInternal, "Failed to set position");
                        }
                        LOG(INFO) << "Set position: " << pos;
                    } else {
                        LOG(WARNING) << "Position " << pos << " outside operational limits ["
                                     << operational_lower_limit_ << ", " << operational_upper_limit_ << "]";
                    }
                }
                
                // Handle duration if specified (for future timing control)
                if(complex_action.has_duration_ms()) {
                    LOG(INFO) << "Action duration: " << complex_action.duration_ms() << "ms";
                    // TODO: Implement duration-based execution
                }
                break;
            }

            case robot::action::ActionPacket::kPosition: {
                float pos = action_packet.position();
                if (pos >= operational_lower_limit_ && pos <= operational_upper_limit_) {
                    if(!SetPosition(pos).ok()) {
                        LOG(ERROR) << "Failed to set position";
                        return absl::Status(absl::StatusCode::kInternal, "Failed to set position");
                    }
                    LOG(INFO) << "Set position: " << pos;
                } else {
                    LOG(WARNING) << "Position " << pos << " outside operational limits ["
                                 << operational_lower_limit_ << ", " << operational_upper_limit_ << "]";
                }
                break;
            }

            case robot::action::ActionPacket::kTorque: {
                float torque = action_packet.torque();
                if (torque >= 0.0f) {
                    if(!SetTorque(torque).ok()) {
                        LOG(ERROR) << "Failed to set torque";
                        return absl::Status(absl::StatusCode::kInternal, "Failed to set torque");
                    }
                    LOG(INFO) << "Set torque: " << torque;
                } else {
                    LOG(WARNING) << "Invalid torque value: " << torque;
                }
                break;
            }

            case robot::action::ActionPacket::kSpeed: {
                float speed = action_packet.speed();
                if (speed >= 0) {
                    if(!SetSpeed(speed).ok()) {
                        LOG(ERROR) << "Failed to set speed";
                        return absl::Status(absl::StatusCode::kInternal, "Failed to set speed");
                    }
                    LOG(INFO) << "Set speed: " << speed;
                } else {
                    LOG(WARNING) << "Invalid speed value: " << speed;
                }
                break;
            }

            case robot::action::ActionPacket::ACTION_TYPE_NOT_SET:
            default:
                LOG(WARNING) << "No action type set in ActionPacket [ID: " << action_packet.action_id() << "]";
                break;
        }
        
        LOG(INFO) << "Action completed successfully [ID: " << action_packet.action_id() << "]";
        
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to execute action [ID: " << action_packet.action_id() 
                   << "]: " << e.what();
        return absl::Status(absl::StatusCode::kInternal, "Failed to execute action");
    }
    return absl::OkStatus();
}

absl::Status Sts3215Driver::SetSpeed(float value) {
    move_speed_ = static_cast<uint16_t>(value);
    return absl::OkStatus();
}

absl::Status Sts3215Driver::SetPosition(float angle) {
    try{
        serial_->Write(create_move_packet(static_cast<uint16_t>(angle)));
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return absl::Status(absl::StatusCode::kInternal, "Failed to set position.");
    }
    return absl::OkStatus();
}

absl::Status Sts3215Driver::SetTorque(float torque) {
    try{
        serial_->Write(create_torque_packet(static_cast<uint16_t>(torque)));
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return absl::Status(absl::StatusCode::kInternal, "Failed to set torque.");
    }
    return absl::OkStatus();
}

absl::StatusOr<std::string> Sts3215Driver::GetId() {
    auto id = "sts3215_driver_" + std::to_string(servo_id_);
    return absl::OkStatus(id);
}

absl::Status Sts3215Driver::SetMiddlePosition(){
    try{
        auto middle_position = (operational_lower_limit_ + operational_upper_limit_)/2;
        serial_->Write(create_move_packet(middle_position));
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return absl::Status(absl::StatusCode::kInternal, "Failed to set middle position.");
    }
    return absl::OkStatus();
}

absl::Status Sts3215Driver::SetIdlePosition(){
    try{
        serial_->Write(create_move_packet(idle_position_));
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return absl::Status(absl::StatusCode::kInternal, "Failed to set idle position.");
    }
    return absl::OkStatus();
}

absl::Status Sts3215Driver::GracefulShutdown(){
    try{
        move_speed_ = 1000;
        if(!SetIdlePosition().ok()) {
            LOG(ERROR) << "Failed to set idle position";
            return absl::Status(absl::StatusCode::kInternal, "Failed to set idle position");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        LOG(INFO) << "Setting torque to 0 (servo " << static_cast<int>(servo_id_) << ")";
        if(!SetTorque(0).ok()) {
            LOG(ERROR) << "Failed to set torque";
            return absl::Status(absl::StatusCode::kInternal, "Failed to set torque");
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return absl::Status(absl::StatusCode::kInternal, "Failed to graceful shutdown.");
    }
    return absl::OkStatus();
}

} // namespace robot::action
