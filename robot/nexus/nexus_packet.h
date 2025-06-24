#pragma once

#include <chrono>

namespace robot::nexus{
using namespace std::chrono;

struct NexusPacket {
    time_point<system_clock, nanoseconds> timestamp;
    std::string sensor_id; 
    SensorDataVariant data_payload;

    // To store in the priority queue in order.
    bool operator>(const NexusPacket& other) const {
            return timestamp > other.timestamp;
        }
}
}