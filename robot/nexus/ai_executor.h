#pragma once

#include "robot/nexus/proto/nexus_packet.pb.h"
#include <pybind11/embed.h>
#include <string>
#include <memory>

namespace robot::nexus {

class AIExecutor {
public:
    AIExecutor();
    ~AIExecutor();

    bool Init(const std::string& module_name, const std::string& function_name);
    NexusModelOutputPacket Predict(const NexusModelInputPacket& input_packet);

private:
    struct PybindData;
    std::unique_ptr<PybindData> pybind_data_;
};

} // namespace robot::nexus 