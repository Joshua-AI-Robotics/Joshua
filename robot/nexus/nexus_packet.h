#pragma once

#include <chrono>

namespace robot::nexus{
using namespace std::chrono;

using SensorDataVariant = std::variant<
    MotorEncoderData,
    CameraFrameData,
    // Add other sensor types as needed
>;


struct NexusPacket {
    time_point<system_clock, nanoseconds> timestamp;
    std::string sensor_id; 
    SensorDataVariant data_payload;
}
}