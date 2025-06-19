#include "robot/onboard/factory/motor_factory.h"
#include <glog/logging.h>

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1; // Log messages to stderr

    LOG(INFO) << "Starting main_program";

    boost::asio::io_context io_context;

    try {
        // sudo usermod -a -G dialout $USER
        // And reboot your machine for update the permissions.

        MotorFactory motor_factory;
        
        // TODO: -1 means this can controll mutliple.
        auto sts3215 = motor_factory.CreateMotor(MotorType::STS3215, io_context, "/dev/ttyACM0", 1000000 ,-1);

        sts3215->SetTorque(1, 1);
        sleep(1);

        sts3215->SetPosition(1, 1200);
        sleep(1);
        sts3215->SetPosition(1, 2800);
        sleep(1);
        sts3215->SetTorque(1, 0);

    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return 1;
    }

    return 0;
}
