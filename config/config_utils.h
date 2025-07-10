#pragma once

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <streambuf>
#include <string>

#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "config/proto/config.pb.h"

namespace config::config_util{

    config::Config LoadConfig(const std::string& config_path) {
    config::Config config;
    std::ifstream input(config_path);       
    if (!input) {
        LOG(ERROR) << "Failed to open config file: " << config_path;
        throw std::runtime_error("Failed to open config file: " + config_path);
    }
    std::string config_content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (!google::protobuf::TextFormat::ParseFromString(config_content, &config)) {
        LOG(ERROR) << "Failed to parse config from file: " << config_path;
        throw std::runtime_error("Failed to parse config from file: " + config_path);
    }
    return config;
    }
}