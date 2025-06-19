#include "robot/onboard/factory/motor_factory.h"
#include "robot/onboard/drivers/xbox_controller/xbox_controller.h"
#include "robot/onboard/drivers/sts3215_driver.h"
#include <glog/logging.h>
#include <vector>
#include <unistd.h> // For sleep
#include <chrono>
#include <thread>
#include <atomic>   // For std::atomic
#include <csignal>  // For signal handling
#include <memory>   // For std::unique_ptr

namespace {

// Constants for servo control
const int NUMBER_OF_SERVOS = 6;
const int START_ID = 1;
const int POSITION_STEP = 10;
const int MOVE_SPEED = 2000;
const int SETUP_MOVE_SPEED = 1200;
const int SETUP_TIME = 2; // seconds
const int LOOP_DELAY_US = 10000; // 10ms delay
const int JOYSTICK_DEADZONE = 5000; // Adjust this value as needed. Typical range is 0-32767

// Servo limits - replicated from the original Python script
struct ServoLimit {
    int min;
    int max;
};

const std::vector<float> END_POSITIONS = {2070, 847, 3011, 655, 1838, 1806};

std::map<int, ServoLimit> SERVO_LIMITS = {
    {0, {1024, 3072}},
    {1, {800, 3000}},
    {2, {950, 3000}},
    {3, {900, 3072}},
    {4, {0, 3000}},
    {5, {1762, 2400}},
};

// Mapping of Xbox controller event codes to servo indices - replicated from Python
std::map<int, int> XBOX_SERVO_MAP = {
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

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1; // Log messages to stderr

    LOG(INFO) << "Starting main_program";

    /*
    TODO:
    2. Make internal queue to avoid race condition.
    3. set speed for each motor now.
    4. Make gripper speed faster.
    */

    try {
        // sudo usermod -a -G dialout $USER
        // And reboot your machine for update the permissions.
        MotorFactory motor_factory;        
       
        boost::asio::io_context io_context;
        auto serial = std::make_shared<Serial>(io_context, "/dev/ttyACM0", 1000000);
        std::vector<std::unique_ptr<MotorInterface>> so100;
        std::vector<int> current_servo_positions(NUMBER_OF_SERVOS);

        for(int i = 0; i < NUMBER_OF_SERVOS; i++){
            so100.emplace_back(motor_factory.CreateMotor(MotorType::STS3215, serial, START_ID + i));
        }
       
        for(int i = 0; i < NUMBER_OF_SERVOS; ++i){
            auto& servo = so100[i];
            servo->SetTorque(1);
            servo->SetSpeed(i == 5 ? SETUP_MOVE_SPEED*2 : SETUP_MOVE_SPEED);
            int middle_position = (SERVO_LIMITS[i].min + SERVO_LIMITS[i].max) / 2;
            current_servo_positions[i] = middle_position;
            servo->SetPosition(middle_position);
        }
        sleep(SETUP_TIME);


        robot::onboard::drivers::XboxController xbox_controller;
        if (!xbox_controller.Init()) {
            LOG(ERROR) << "Failed to initialize Xbox controller. Exiting.";
            return 1;
        }
        robot::onboard::drivers::XboxControllerState controller_state;
        std::thread controller_thread([&]() { // Pass by reference for controller_state
            xbox_controller.Run(controller_state);
        });

        // Main loop to read from XboxControllerState and send commands to servos
        while (true) {
            // Check for Start button press to escape the loop
            if (controller_state.btn_start_state == 1) {
                LOG(INFO) << "Start button pressed. Exiting main loop.";
                break; 
            }

            // Process controller state and update servo positions
            // D-Pad X
            int servo_index_0 = XBOX_SERVO_MAP[ABS_HAT0X];
            if (controller_state.abs_hat0x_value == 1) {
                current_servo_positions[servo_index_0] += POSITION_STEP / 2;
            } else if (controller_state.abs_hat0x_value == -1) {
                current_servo_positions[servo_index_0] -= POSITION_STEP / 2;
            }

            // Left Joystick Y
            int servo_index_1 = XBOX_SERVO_MAP[ABS_Y];
            int joystick_y_value = controller_state.abs_y_value;
            if (std::abs(joystick_y_value) < JOYSTICK_DEADZONE) {
                joystick_y_value = 0;
            }
            int mapped_value_y = MapRange(joystick_y_value, -32768, 32767, -POSITION_STEP, POSITION_STEP);
            if (mapped_value_y != 0) {
                current_servo_positions[servo_index_1] += mapped_value_y;
            }

            // Right Joystick Y
            int servo_index_2 = XBOX_SERVO_MAP[ABS_RY];
            int joystick_ry_value = controller_state.abs_ry_value;
            if (std::abs(joystick_ry_value) < JOYSTICK_DEADZONE) {
                joystick_ry_value = 0;
            }
            int mapped_value_ry = MapRange(joystick_ry_value, -32768, 32767, -POSITION_STEP, POSITION_STEP);
            if (mapped_value_ry != 0) {
                current_servo_positions[servo_index_2] += mapped_value_ry;
            }

            // Y Button
            int servo_index_3_y = XBOX_SERVO_MAP[BTN_WEST];
            if (controller_state.btn_west_state == 1) {
                current_servo_positions[servo_index_3_y] -= POSITION_STEP;
            }
            
            // A Button
            int servo_index_3_a = XBOX_SERVO_MAP[BTN_SOUTH];
            if (controller_state.btn_south_state == 1) {
                current_servo_positions[servo_index_3_a] += POSITION_STEP;
            }

            // Left Bumper
            int servo_index_4_lb = XBOX_SERVO_MAP[BTN_TL];
            if (controller_state.btn_tl_state == 1) {
                current_servo_positions[servo_index_4_lb] += POSITION_STEP;
            }

            // Right Bumper
            int servo_index_4_rb = XBOX_SERVO_MAP[BTN_TR];
            if (controller_state.btn_tr_state == 1) {
                current_servo_positions[servo_index_4_rb] -= POSITION_STEP;
            }

            // Right Trigger
            int servo_index_5 = XBOX_SERVO_MAP[ABS_RZ];
            current_servo_positions[servo_index_5] = MapRange(controller_state.abs_rz_value, 0, 1023, SERVO_LIMITS[servo_index_5].max, SERVO_LIMITS[servo_index_5].min);

            // Apply servo limits and send commands
            for (int i = 0; i < NUMBER_OF_SERVOS; ++i) {
                if (current_servo_positions[i] > SERVO_LIMITS[i].max) {
                    current_servo_positions[i] = SERVO_LIMITS[i].max;
                }
                if (current_servo_positions[i] < SERVO_LIMITS[i].min) {
                    current_servo_positions[i] = SERVO_LIMITS[i].min;
                }
                auto& servo = so100[i];
                servo->SetPosition(current_servo_positions[i]);
            }            
            usleep(LOOP_DELAY_US);
        }

        LOG(INFO) << "Shutting down...";
        for (int i = 0; i < NUMBER_OF_SERVOS; ++i) {
            auto& servo = so100[i];
            servo->SetPosition(END_POSITIONS[i]);            
        }
        sleep(SETUP_TIME);
        
        LOG(INFO) << "Disabling torque on all servos...";
        for(auto& servo : so100){
            servo->SetTorque(0);
        }

        controller_thread.join(); // Join the controller thread before exiting

    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return 1;
    }

    

    return 0;
}
