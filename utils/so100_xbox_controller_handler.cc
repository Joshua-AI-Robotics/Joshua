#include "utils/so100_xbox_controller_handler.h"
#include "robot/actuation/motors/drivers/sts3215_driver.h"
#include <glog/logging.h>
#include <unistd.h>
#include <chrono>
#include <thread>

namespace utils {

So100XboxControllerHandler::So100XboxControllerHandler(const robot::Robot& robot_config,
                                           std::vector<std::unique_ptr<robot::actuation::MotorInterface>>& motors)
    : robot_config_(robot_config), motors_(motors) {}

So100XboxControllerHandler::~So100XboxControllerHandler() {
    Stop();
    join();
}

bool So100XboxControllerHandler::Init() {
    if (!xbox_controller_.Init()) {
        LOG(ERROR) << "Failed to initialize Xbox controller.";
        return false;
    }
    return true;
}

void So100XboxControllerHandler::Start() {
    if (xbox_event_thread_.joinable()) {
        LOG(WARNING) << "Xbox event listener thread already running.";
    } else {
         xbox_event_thread_ = std::thread([this]() {
            xbox_controller_.Run(controller_state_);
        });
    }

    if (motor_control_thread_.joinable()) {
        LOG(WARNING) << "Motor control thread already running.";
    } else {
        stop_flag_ = false;
        motor_control_thread_ = std::thread(&So100XboxControllerHandler::control_loop, this);
    }

    join();
}

void So100XboxControllerHandler::Stop() {
    stop_flag_ = true;
}

int So100XboxControllerHandler::MapRange(int value, int in_min, int in_max, int out_min, int out_max) {
    if (in_max == in_min) return out_min;
    return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void So100XboxControllerHandler::join(){
    if (xbox_event_thread_.joinable()) {
         if (xbox_event_thread_.joinable()) {
            xbox_event_thread_.join();
        }
    }
    if (motor_control_thread_.joinable()) {
        motor_control_thread_.join();
    }
}

void So100XboxControllerHandler::control_loop() {
    std::vector<int> current_servo_positions(motors_.size());
    for(size_t i = 0; i < motors_.size(); ++i){
        if(motors_[i]){
            current_servo_positions[i] = static_cast<int>(motors_[i]->GetPosition());
        }
    }

    while (!stop_flag_) {
        if (controller_state_.btn_start_state == 1) {
            LOG(INFO) << "Start button pressed. Signaling stop.";
            stop_flag_ = true;
            break; 
        }

        // Process controller state and update servo positions
        // D-Pad X
        if (kXboxServoMap_.count(ABS_HAT0X) && motors_.size() > static_cast<size_t>(kXboxServoMap_.at(ABS_HAT0X))) {
            int servo_index_0 = kXboxServoMap_.at(ABS_HAT0X);
            if (controller_state_.abs_hat0x_value == 1) {
                current_servo_positions[servo_index_0] += kPositionStep / 2;
            } else if (controller_state_.abs_hat0x_value == -1) {
                current_servo_positions[servo_index_0] -= kPositionStep / 2;
            }
        }

        // Left Joystick Y
        if (kXboxServoMap_.count(ABS_Y) && motors_.size() > static_cast<size_t>(kXboxServoMap_.at(ABS_Y))) {
            int servo_index_1 = kXboxServoMap_.at(ABS_Y);
            int joystick_y_value = controller_state_.abs_y_value;
            if (std::abs(joystick_y_value) < kJoystickDeadZone) {
                joystick_y_value = 0;
            }
            int mapped_value_y = MapRange(joystick_y_value, -32768, 32767, -kPositionStep, kPositionStep);
            if (mapped_value_y != 0) {
                current_servo_positions[servo_index_1] += mapped_value_y;
            }
        }

        // Right Joystick Y
        if (kXboxServoMap_.count(ABS_RY) && motors_.size() > static_cast<size_t>(kXboxServoMap_.at(ABS_RY))) {
            int servo_index_2 = kXboxServoMap_.at(ABS_RY);
            int joystick_ry_value = controller_state_.abs_ry_value;
            if (std::abs(joystick_ry_value) < kJoystickDeadZone) {
                joystick_ry_value = 0;
            }
            int mapped_value_ry = MapRange(joystick_ry_value, -32768, 32767, -kPositionStep, kPositionStep);
            if (mapped_value_ry != 0) {
                current_servo_positions[servo_index_2] += mapped_value_ry;
            }
        }
        
        // Y Button (BTN_WEST) & A Button (BTN_SOUTH) for servo 3
        if (kXboxServoMap_.count(BTN_WEST) && motors_.size() > static_cast<size_t>(kXboxServoMap_.at(BTN_WEST))) {
            int servo_index_3 = kXboxServoMap_.at(BTN_WEST); // Both map to the same servo
            if (controller_state_.btn_west_state == 1) { // Y button
                current_servo_positions[servo_index_3] -= kPositionStep;
            }
            if (controller_state_.btn_south_state == 1) { // A button
                current_servo_positions[servo_index_3] += kPositionStep;
            }
        }

        // Left Bumper (BTN_TL) & Right Bumper (BTN_TR) for servo 4
        if (kXboxServoMap_.count(BTN_TL) && motors_.size() > static_cast<size_t>(kXboxServoMap_.at(BTN_TL))) {
            int servo_index_4 = kXboxServoMap_.at(BTN_TL); // Both map to the same servo
            if (controller_state_.btn_tl_state == 1) { // Left Bumper
                current_servo_positions[servo_index_4] += kPositionStep;
            }
            if (controller_state_.btn_tr_state == 1) { // Right Bumper
                current_servo_positions[servo_index_4] -= kPositionStep;
            }
        }
        
        // Right Trigger (ABS_RZ) for servo 5
        if (kXboxServoMap_.count(ABS_RZ) && motors_.size() > static_cast<size_t>(kXboxServoMap_.at(ABS_RZ))) {
            int servo_index_5 = kXboxServoMap_.at(ABS_RZ);
            // Ensure motor config is accessible and valid for this motor index
            if (servo_index_5 < robot_config_.actuations().single_actuation_size() && robot_config_.actuations().single_actuation(servo_index_5).motor().has_sts3215_config()) {
                 current_servo_positions[servo_index_5] = MapRange(controller_state_.abs_rz_value, 0, 1023,
                    robot_config_.actuations().single_actuation(servo_index_5).motor().sts3215_config().operational_upper_limit(),
                    robot_config_.actuations().single_actuation(servo_index_5).motor().sts3215_config().operational_lower_limit());
            } else {
                // LOG_FIRST_N(WARNING, 10) << "Motor " << servo_index_5 << " missing STS3215 config or out of bounds for trigger mapping.";
            }
        }

        // Apply servo limits and send commands
        for (size_t i = 0; i < motors_.size(); ++i) {
            if (!motors_[i]) continue;

            // Ensure motor config is accessible and valid for this motor index
            if (i < static_cast<size_t>(robot_config_.actuations().single_actuation_size()) && robot_config_.actuations().single_actuation(i).motor().has_sts3215_config()){
                const auto& motor_conf = robot_config_.actuations().single_actuation(i).motor().sts3215_config();
                if (current_servo_positions[i] > motor_conf.operational_upper_limit()) {
                    current_servo_positions[i] = motor_conf.operational_upper_limit();
                }
                if (current_servo_positions[i] < motor_conf.operational_lower_limit()) {
                    current_servo_positions[i] = motor_conf.operational_lower_limit();
                }
            } else {
                // LOG_FIRST_N(WARNING, 10) << "Motor " << i << " missing STS3215 config or out of bounds for limit application.";
            }
            motors_[i]->SetPosition(current_servo_positions[i]);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Same as usleep(10000)
    }
    LOG(INFO) << "So100XboxControllerHandler ControlLoop stopped.";
}

} // namespace utils