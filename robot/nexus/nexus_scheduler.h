#pragma once

#include <chrono>
#include <thread>

namespace robot::nexus {

class NexusScheduler {
public:
    explicit NexusScheduler(int frequency)
        : period_(frequency > 0 ? 1000 / frequency : 0),
          next_trigger_time_(std::chrono::steady_clock::now()) {}

    void wait_for_next_trigger() {
        next_trigger_time_ += period_;
        std::this_thread::sleep_until(next_trigger_time_);
    }

private:
    std::chrono::milliseconds period_;
    std::chrono::steady_clock::time_point next_trigger_time_;
};

} // namespace robot::nexus
