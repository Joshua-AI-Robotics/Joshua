#pragma once

#include "utils/xbox_controller/xbox_controller.h"
#include "robot/config/robot.pb.h"
#include "robot/actuation/interfaces/actuation_interface.h"
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <map>

namespace utils {

class So100XboxControllerHandler {
public:
    So100XboxControllerHandler(const robot::Robot& robot_config, 
                          std::vector<std::unique_ptr<robot::actuation::ActuationInterface>>& actuators);
    ~So100XboxControllerHandler();

    bool Init();
    void Start();
    void Stop();
    
    void Join();

private:
    void ControlLoop();
    int MapRange(int value, int in_min, int in_max, int out_min, int out_max);

    const robot::Robot& robot_config_;
    std::vector<std::unique_ptr<robot::actuation::ActuationInterface>>& actuators_;
    utils::XboxController xbox_controller_;
    utils::XboxControllerState controller_state_;
    std::thread xbox_event_thread_; // Reading the xbox status.
    std::thread motor_control_thread_; // Main ControlLoop.
    std::atomic<bool> stop_flag_{false};

    static constexpr int kJoystickDeadZone = 5000;
    static constexpr auto kPositionStep = 10;
    
    std::map<int, int> kXboxServoMap_ = {
        {ABS_HAT0X, 0},
        {ABS_Y, 1},
        {ABS_RY, 2},
        {BTN_WEST, 3}, // Corresponds to Y button for decreasing position
        {BTN_SOUTH, 3},// Corresponds to A button for increasing position
        {BTN_TL, 4},   // Left Bumper
        {BTN_TR, 4},   // Right Bumper
        {ABS_RZ, 5}    // Right Trigger
    };
};

} // namespace utils
