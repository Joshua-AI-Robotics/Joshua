#pragma once

#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <stdexcept>

#include <glog/logging.h>

namespace robot::nexus {

class NexusScheduler {
public:
    explicit NexusScheduler(int frequency) {
        SetFrequency(frequency);
        start_time_ = std::chrono::steady_clock::now();
        next_trigger_time_ = start_time_;
    }

    void WaitForNextTrigger() {
        std::unique_lock<std::mutex> lock(mtx_);
        if (stop_flag_) {
            return;
        }

        if (frequency_ == 0) {
            return;
        }

        next_trigger_time_ += period_;

        auto time_since_start = std::chrono::duration_cast<std::chrono::microseconds>(
            next_trigger_time_ - start_time_);
        LOG(INFO) << "Next trigger at: " << time_since_start.count() / 1000.0 << " ms";
        
        cv_.wait_until(lock, next_trigger_time_);
    }

    int GetFrequency() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return frequency_;
    }

    void SetFrequency(int frequency) {
        if (frequency < 0) {
            throw std::invalid_argument("Frequency cannot be negative.");
        }
        bool should_notify = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            int old_frequency = frequency_;
            frequency_ = frequency;

            if (frequency_ > 0) {
                if (old_frequency == 0) {
                    next_trigger_time_ = std::chrono::steady_clock::now();
                }
                period_ = std::chrono::nanoseconds(static_cast<long long>(1'000'000'000.0 / frequency_));
            } else {
                period_ = std::chrono::nanoseconds(0);
            }
            
            if (old_frequency != frequency_) {
                should_notify = true;
            }
        }
        if (should_notify) {
            cv_.notify_all();
        }
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_flag_ = true;
        }
        cv_.notify_all();
    }

private:
    std::chrono::nanoseconds period_{0};
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point next_trigger_time_;
    int frequency_ = 0;
    bool stop_flag_ = false;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
};

} // namespace robot::nexus
