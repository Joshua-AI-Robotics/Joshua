#pragma once

#include "robot/nexus/proto/nexus_packet.pb.h"
#include "config/proto/config.pb.h"
#include <string>
#include <memory>

namespace robot::nexus {

class AIExecutor {
public:
    explicit AIExecutor(const config::Config& config);
    ~AIExecutor();

    bool Init();
    NexusModelOutputPacket Inference(const NexusModelInputPacket& input_packet);

private:
    struct PybindData;
    std::unique_ptr<PybindData> pybind_data_;
    config::Config config_;
};

} // namespace robot::nexus 