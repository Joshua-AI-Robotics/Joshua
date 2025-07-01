#pragma once

#include <memory>
#include "robot/nexus/proto/nexus_packet.pb.h"

namespace robot::perception {

class PerceptionInterface {
public:
    virtual ~PerceptionInterface() = default;

    // A generic method to get data from the sensor.
    // The specific data type will be inside the NexusPerceptionPacket.
    virtual std::unique_ptr<robot::nexus::NexusPerceptionPacket> GetData() = 0;

    // A method to trigger a capture or reading from the sensor.
    virtual void Capture() = 0;

    virtual std::string GetId() = 0;
};

} // namespace robot::perception 