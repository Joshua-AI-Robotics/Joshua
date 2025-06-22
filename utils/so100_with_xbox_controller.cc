#include "robot/actuation/factory/motor_factory.h"
#include "utils/xbox_controller/xbox_controller.h"
#include "robot/actuation/motors/drivers/sts3215_driver.h"
#include <glog/logging.h>
#include <vector>
#include <unistd.h> // For sleep
#include <chrono>
#include <thread>
#include <atomic>   // For std::atomic
#include <memory>   // For std::unique_ptr
#include <google/protobuf/text_format.h>
#include <fstream>


namespace{
    constexpr int kSetupTime = 2;

    std::map<int, int> kXboxServoMap = {
    {ABS_HAT0X, 0},
    {ABS_Y, 1},
    {ABS_RY, 2},
    {BTN_WEST, 3},
    {BTN_SOUTH, 3},
    {BTN_TL, 4},
    {BTN_TR, 4},
    {ABS_RZ, 5}
};

// Helper function to map a value from one range to another
int MapRange(int value, int in_min, int in_max, int out_min, int out_max) {
    return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
} // namespace

robot_config::Robot LoadRobotConfig(const std::string& config_path) {
    robot_config::Robot robot_config;
    std::ifstream input(config_path);
    if (!input) {
        LOG(ERROR) << "Failed to open robot config file: " << config_path;
        throw std::runtime_error("Failed to open robot config file: " + config_path);
    }
    std::string config_content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (!google::protobuf::TextFormat::ParseFromString(config_content, &robot_config)) {
        LOG(ERROR) << "Failed to parse robot config from file: " << config_path;
        throw std::runtime_error("Failed to parse robot config from file: " + config_path);
    }
    return robot_config;
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1; // Log messages to stderr

    try {
        // sudo usermod -a -G dialout $USER
        // And reboot your machine for update the permissions.

        utils::XboxController xbox_controller;
        if (!xbox_controller.Init()) {
            LOG(ERROR) << "Failed to initialize Xbox controller. Exiting.";
            return 1;
        }
        utils::XboxControllerState controller_state;
        std::thread controller_thread([&]() { // Pass by reference for controller_state
            xbox_controller.Run(controller_state);
        });

        robot_config::Robot robot_config = LoadRobotConfig("robot/config/robot_config.pbtxt");
        auto number_of_motors = robot_config.motor_size();
        LOG(INFO) << "Robot Name: " << robot_config.name();
        LOG(INFO) << "ID:" << robot_config.id();

        // Motor instantiation.
        robot::actuation::MotorFactory motor_factory; 
        std::vector<std::unique_ptr<robot::actuation::MotorInterface>> motors;

        for(int i = 0; i < number_of_motors; i++){
            const auto& motor_proto = robot_config.motor(i);

            switch(motor_proto.motor_type()){
                case robot_config::MotorType::STS3215:
                    motors.emplace_back(motor_factory.CreateMotor(motor_proto));
                    break;
                default:
                    LOG(ERROR) << "Unknown motor type: " << motor_proto.motor_type();
                    break;
            }
        }
       
        // Random servo movements for validation.
        for(int i = 0; i < number_of_motors; ++i){
            auto& servo = motors[i];
            servo->SetTorque(1);
            servo->SetMiddlePosition();
        }
        sleep(kSetupTime);

        const int POSITION_STEP = 10;
        const int JOYSTICK_DEADZONE = 5000;
        std::vector<int> current_servo_positions(6);
        for(int i = 0; i < number_of_motors; i++){
            current_servo_positions[i] = int(motors[i]->GetPosition());
            LOG(INFO) << motors[i]->GetPosition();
        }
        
        // Main loop to read from XboxControllerState and send commands to servos
        while (true) {
            // Check for Start button press to escape the loop
            if (controller_state.btn_start_state == 1) {
                LOG(INFO) << "Start button pressed. Exiting main loop.";
                break; 
            }

            // Process controller state and update servo positions
            // D-Pad X
            int servo_index_0 = kXboxServoMap[ABS_HAT0X];
            if (controller_state.abs_hat0x_value == 1) {
                current_servo_positions[servo_index_0] += POSITION_STEP / 2;
            } else if (controller_state.abs_hat0x_value == -1) {
                current_servo_positions[servo_index_0] -= POSITION_STEP / 2;
            }

            // Left Joystick Y
            int servo_index_1 = kXboxServoMap[ABS_Y];
            int joystick_y_value = controller_state.abs_y_value;
            if (std::abs(joystick_y_value) < JOYSTICK_DEADZONE) {
                joystick_y_value = 0;
            }
            int mapped_value_y = MapRange(joystick_y_value, -32768, 32767, -POSITION_STEP, POSITION_STEP);
            if (mapped_value_y != 0) {
                current_servo_positions[servo_index_1] += mapped_value_y;
            }

            // Right Joystick Y
            int servo_index_2 = kXboxServoMap[ABS_RY];
            int joystick_ry_value = controller_state.abs_ry_value;
            if (std::abs(joystick_ry_value) < JOYSTICK_DEADZONE) {
                joystick_ry_value = 0;
            }
            int mapped_value_ry = MapRange(joystick_ry_value, -32768, 32767, -POSITION_STEP, POSITION_STEP);
            if (mapped_value_ry != 0) {
                current_servo_positions[servo_index_2] += mapped_value_ry;
            }

            // Y Button
            int servo_index_3_y = kXboxServoMap[BTN_WEST];
            if (controller_state.btn_west_state == 1) {
                current_servo_positions[servo_index_3_y] -= POSITION_STEP;
            }
            
            // A Button
            int servo_index_3_a = kXboxServoMap[BTN_SOUTH];
            if (controller_state.btn_south_state == 1) {
                current_servo_positions[servo_index_3_a] += POSITION_STEP;
            }

            // Left Bumper
            int servo_index_4_lb = kXboxServoMap[BTN_TL];
            if (controller_state.btn_tl_state == 1) {
                current_servo_positions[servo_index_4_lb] += POSITION_STEP;
            }

            // Right Bumper
            int servo_index_4_rb = kXboxServoMap[BTN_TR];
            if (controller_state.btn_tr_state == 1) {
                current_servo_positions[servo_index_4_rb] -= POSITION_STEP;
            }

            // Right Trigger
            int servo_index_5 = kXboxServoMap[ABS_RZ];
            current_servo_positions[servo_index_5] = MapRange(controller_state.abs_rz_value, 0, 1023,
             robot_config.motor(servo_index_5).sts3215_config().operational_upper_limit(),
             robot_config.motor(servo_index_5).sts3215_config().operational_lower_limit());

            // Apply servo limits and send commands
            for (int i = 0; i < number_of_motors; ++i) {
                if (current_servo_positions[i] > robot_config.motor(i).sts3215_config().operational_upper_limit()) {
                    current_servo_positions[i] = robot_config.motor(i).sts3215_config().operational_upper_limit();
                }
                if (current_servo_positions[i] < robot_config.motor(i).sts3215_config().operational_lower_limit()) {
                    current_servo_positions[i] = robot_config.motor(i).sts3215_config().operational_lower_limit();
                }
                auto& servo = motors[i];
                servo->SetPosition(current_servo_positions[i]);
            }
            usleep(10000);
        }

        LOG(INFO) << "Shutting down...";
        for (int i = 0; i < number_of_motors; ++i) {
            auto& servo = motors[i];
            servo->SetIdlePosition();     
        }
        sleep(kSetupTime);
        
        LOG(INFO) << "Disabling torque on all servos...";
        for(auto& servo : motors){
            servo->SetTorque(0);
        }

        controller_thread.join(); // Join the controller thread before exiting

    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return 1;
    }

    

    return 0;
}
