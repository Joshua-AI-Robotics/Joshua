#include "config/proto/config.pb.h"
#include "config/config_utils.h"
#include <map>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <vector>
#include <chrono>
#include <thread>

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    config::Config config = config::config_util::LoadConfig("config/config_preset/so100_with_example_ai.pbtxt");
    auto robot_config = config.robot();
    auto ai_config = config.ai();

    LOG(INFO) << "Robot Name: " << robot_config.name();
    LOG(INFO) << "Actuator Size: " << robot_config.actuations().single_actuation_size();
    LOG(INFO) << "Perception Size: " << robot_config.perceptions().single_perception_size();

    return 0;
}
