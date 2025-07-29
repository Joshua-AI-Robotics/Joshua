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

    robot::perception::Sensor GetSensorConfigFromString(const std::string& config_str) {
        robot::perception::Sensor sensor;
        if (!google::protobuf::TextFormat::ParseFromString(config_str, &sensor)) {
            LOG(ERROR) << "Failed to parse config from string: " << config_str;
            throw std::runtime_error("Failed to parse config from string: " + config_str);
        }
        return sensor;
    }

    // Utility function to base64 encode config string for safe process argument passing
    std::string EscapeConfigString(const std::string& config_content) {
        std::string result;
        const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        for (size_t i = 0; i < config_content.length(); i += 3) {
            uint32_t val = 0;
            int padding = 0;
            
            for (int j = 0; j < 3; j++) {
                val <<= 8;
                if (i + j < config_content.length()) {
                    val |= (unsigned char)config_content[i + j];
                } else {
                    padding++;
                }
            }
            
            for (int j = 0; j < 4; j++) {
                if (j < 4 - padding) {
                    result += chars[(val >> (18 - 6 * j)) & 0x3F];
                } else {
                    result += '=';
                }
            }
        }
        
        return result;
    }

    // Utility function to base64 decode config string back to original format
    std::string DecodeConfigString(const std::string& encoded_config) {
        std::string config_str;
        const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        for (size_t i = 0; i < encoded_config.length(); i += 4) {
            uint32_t val = 0;
            int padding = 0;
            
            for (int j = 0; j < 4; j++) {
                val <<= 6;
                if (i + j < encoded_config.length()) {
                    char c = encoded_config[i + j];
                    if (c == '=') {
                        padding++;
                    } else {
                        size_t pos = chars.find(c);
                        if (pos != std::string::npos) {
                            val |= pos;
                        }
                    }
                }
            }
            
            for (int j = 0; j < 3 - padding; j++) {
                config_str += (char)((val >> (16 - 8 * j)) & 0xFF);
            }
        }
        
        return config_str;
    }
}