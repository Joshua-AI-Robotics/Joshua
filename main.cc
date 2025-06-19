#include "robot/comm_interface/serial/serial.h"
#include <glog/logging.h>
#include <iostream>

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1; // Log messages to stderr

    LOG(INFO) << "Starting main_program";

    boost::asio::io_context io_context;

    try {
        // sudo usermod -a -G dialout $USER
        // And reboot your machine for update the permissions.
        Serial serial_port(io_context, "/dev/ttyACM0", 1000000);
        LOG(INFO) << "Serial port opened successfully.";
        serial_port.Write("Hello from Bazel!\n");
        LOG(INFO) << "Sent data: Hello from Bazel!";

    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return 1;
    }

    return 0;
}
