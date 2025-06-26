#pragma once

#include "robot/nexus/proto/nexus_packet.pb.h"
#include <vector>

namespace robot::perception {

class CameraInterface {
public:
    CameraInterface() = default;
    virtual ~CameraInterface() = default;

    virtual void Capture() = 0;
    virtual ::robot::nexus::NexusPerceptionPacket GetData() = 0;
};

} // namespace robot::perception 