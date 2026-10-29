#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

#include "rclcpp/rclcpp.hpp"
#include "utils/robot_time.h"

namespace ros2_utils {
namespace internal {

inline std::function<void()> MakeRobotTimerCallback(double period, std::function<void()> callback) {
  if (!std::isfinite(period) || period < 1e-9 || !callback)
    throw std::invalid_argument("Robot timer requires a finite period >= 1 ns and a callback.");
  const double now = RobotTime();
  if (!std::isfinite(now)) throw std::runtime_error("RobotTime is not finite.");
  double next = now + period;
  if (!std::isfinite(next) || next <= now)
    throw std::invalid_argument("Robot timer period cannot advance the current RobotTime.");

  return [period, next, callback = std::move(callback)]() mutable {
    const double now = RobotTime();
    if (!std::isfinite(now)) throw std::runtime_error("RobotTime is not finite.");
    if (now < next) return;
    next += period;
    // Preserve cadence for small delays; skip missed periods without a burst.
    if (next <= now) next = now + period;
    callback();
  };
}

}  // namespace internal

// Period is in RobotTime seconds. The ROS timer only wakes the executor to
// check the deadline; a paused/backward clock waits for that deadline.
inline rclcpp::TimerBase::SharedPtr CreateRobotTimer(rclcpp::Node& node,
                                                   double period,
                                                   std::function<void()> callback) {
  auto tick = internal::MakeRobotTimerCallback(period, std::move(callback));
  constexpr double kMaxWakeupPeriod = 0.001;
  return node.create_wall_timer(std::chrono::duration<double>(std::min(period, kMaxWakeupPeriod)),
                               std::move(tick));
}

}  // namespace ros2_utils
