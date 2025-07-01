#include "robot/actuation/factory/actuation_factory.h"
#include "robot/config/config_utils.h"
#include "utils/so100_xbox_controller_handler.h"
#include <glog/logging.h>
#include <vector>
#include <unistd.h> // For sleep
#include <memory>   // For std::unique_ptr

namespace {
    constexpr int kSetupTime = 2;
} // namespace

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1; // Log messages to stderr

    try {
        robot::Robot robot_config = robot::config_util::LoadRobotConfig("robot/config/robot_config.pbtxt");
        auto number_of_motors = robot_config.actuations().single_actuation_size();
        LOG(INFO) << "Robot Name: " << robot_config.name();
        LOG(INFO) << "ID:" << robot_config.id();

        // Motor instantiation.
        robot::actuation::ActuationFactory actuation_factory;
        std::vector<std::unique_ptr<robot::actuation::ActuationInterface>> actuators;

        for (int i = 0; i < number_of_motors; i++) {
            const auto& single_actuation = robot_config.actuations().single_actuation(i);
            const auto& motor_proto = single_actuation.motor();

            switch (motor_proto.motor_type()) {
            case robot::actuation::MotorType::STS3215:
                actuators.emplace_back(actuation_factory.CreateActuator(motor_proto));
                break;
            default:
                LOG(ERROR) << "Unknown motor type: " << motor_proto.motor_type();
                break;
            }
        }

        // Initial motor setup.
        for (int i = 0; i < number_of_motors; ++i) {
            if (actuators[i]) {
                actuators[i]->SetTorque(1);
                actuators[i]->SetMiddlePosition();
            }
        }
        sleep(kSetupTime);

        // Initialize and start So100XboxControllerHandler
        utils::So100XboxControllerHandler controller_handler(robot_config, actuators);
        if (!controller_handler.Init()) {
            LOG(ERROR) << "Failed to initialize So100XboxControllerHandler. Exiting.";
            return 1;
        }

        
        controller_handler.Start();
        controller_handler.Join(); // This is a blocking thread.

        for (int i = 0; i < number_of_motors; ++i) {
            if (actuators[i]) {
                actuators[i]->SetIdlePosition();
            }
        }
        sleep(kSetupTime);

        LOG(INFO) << "Disabling torque on all servos...";
        for (auto& servo : actuators) {
            if (servo) {
                servo->SetTorque(0);
            }
        }

    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return 1;
    }

    LOG(INFO) << "Program terminated successfully.";
    return 0;
}
