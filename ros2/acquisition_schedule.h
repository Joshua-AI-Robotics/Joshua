#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "robot/perception/proto/perception.pb.h"
#include "ros2/utils/robot_timer.h"

namespace ros2_utils {

class AcquisitionSchedule {
 public:
  rclcpp::TimerBase::SharedPtr CreateTimer(rclcpp::Node& node,
                                           const robot::perception::SinglePerception& sensor) {
    uint32_t rate = 1;
    for (const auto& output : sensor.node().publishers())
      rate = std::max(rate, output.publish_rate_hz());
    return CreateRobotTimer(node, 1.0 / rate, [this] {
      std::lock_guard lock(mutex_);
      due_ = true;
      wake_.notify_one();
    });
  }

  bool WaitForRead() {
    std::unique_lock lock(mutex_);
    wake_.wait(lock, [&] { return due_ || stopped_; });
    due_ = false;
    return !stopped_;
  }

  void Stop() {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    wake_.notify_one();
  }

  bool stopped() const {
    return stopped_;
  }

 private:
  std::mutex mutex_;
  std::condition_variable wake_;
  std::atomic<bool> stopped_{false};
  bool due_ = true;
};

}  // namespace ros2_utils
