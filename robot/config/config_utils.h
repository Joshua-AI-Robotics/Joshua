#pragma once
#include <google/protobuf/text_format.h>
#include <fstream>
#include "robot/config/robot.pb.h"


namespace robot::config_util{
    robot::Robot LoadRobotConfig(const std::string& config_path) {
    robot::Robot robot_config;
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
}