#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/action/proto/action_packet.pb.h"
#include "ros2/node_runner.h"
#include "ros2/proto/node.pb.h"
#include "ros2/proto/ros2_data_type.pb.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/float32.hpp"

class TrajectoryPublisher : public rclcpp::Node {
 public:
  TrajectoryPublisher(const std::string& node_name, int node_id, const config::Config& config)
      : Node(node_name) {
    for (const auto& single : config.robot().trajectories().single_trajectories()) {
      const auto& node = single.node();
      if (node.node_type() != ros2::node::TRAJECTORY_PUBLISHER ||
          node.id() != static_cast<uint32_t>(node_id)) {
        continue;
      }
      for (const auto& publisher : node.publishers()) {
        if (publisher.ros2_data_type() != ros2::data_type::FLOAT32) {
          throw std::invalid_argument("Trajectory publishers support only FLOAT32: " +
                                      publisher.topic());
        }
      }
      for (const auto& waypoint : single.trajectory().waypoints()) {
        const double seconds = waypoint.timestamp_sec();
        // Stay within the duration representation used by ROS wall timers.
        const double max_seconds =
            std::chrono::duration<double>(std::chrono::nanoseconds::max() / 2).count();
        if (!std::isfinite(seconds) || seconds < 0 || seconds > max_seconds ||
            waypoint.topic().empty()) {
          throw std::invalid_argument(
              "Trajectory requires a finite, nonnegative timestamp "
              "within the timer range and a nonempty topic");
        }
        const float value = ScalarValue(waypoint.action());
        if (!std::isfinite(value)) {
          throw std::invalid_argument("Trajectory values must be finite");
        }
        const auto& topic = waypoint.topic();
        if (publishers_.count(topic) == 0) {
          // A waypoint without an explicit publisher retains the FLOAT32 default.
          publishers_[topic] = create_publisher<std_msgs::msg::Float32>(
              topic, ros2_utils::CreateQosSetting(node.qos_setting()));
        }
        waypoints_.push_back({seconds, topic, value});
      }
    }
    if (waypoints_.empty()) {
      throw std::invalid_argument("No trajectory waypoints for node_id " + std::to_string(node_id));
    }
    // Preserve config order for simultaneous waypoints, including across entries.
    std::stable_sort(waypoints_.begin(),
                     waypoints_.end(),
                     [](const Waypoint& a, const Waypoint& b) { return a.seconds < b.seconds; });
    if (waypoints_.back().seconds <= 0) {
      throw std::invalid_argument("A repeating trajectory must have positive duration");
    }
    timer_ = create_wall_timer(std::chrono::seconds(1), [this]() { WaitForSubscribers(); });
  }

 private:
  using Clock = std::chrono::steady_clock;
  struct Waypoint {
    double seconds;
    std::string topic;
    float value;
  };

  static float ScalarValue(const robot::action::ActionPacket& action) {
    switch (action.action_type_case()) {
      case robot::action::ActionPacket::kPosition:
        return action.position();
      case robot::action::ActionPacket::kSpeed:
        return action.speed();
      case robot::action::ActionPacket::kTorque:
        return action.torque();
      case robot::action::ActionPacket::kDc:
        return action.dc();
      default:
        throw std::invalid_argument(
            "Trajectory FLOAT32 messages require a scalar action "
            "(position, speed, torque, or dc)");
    }
  }

  void WaitForSubscribers() {
    for (const auto& [topic, publisher] : publishers_) {
      if (publisher->get_subscription_count() == 0) {
        RCLCPP_INFO(get_logger(), "Waiting for subscribers on '%s'...", topic.c_str());
        return;
      }
    }
    timer_->cancel();
    loop_start_ = Clock::now();
    RCLCPP_INFO(get_logger(),
                "Starting trajectory: %zu waypoints, %.3fs per loop",
                waypoints_.size(),
                waypoints_.back().seconds);
    PublishDueWaypoints();
  }

  void PublishDueWaypoints() {
    timer_->cancel();
    const double elapsed = std::chrono::duration<double>(Clock::now() - loop_start_).count();
    while (next_ < waypoints_.size() && waypoints_[next_].seconds <= elapsed) {
      const auto& waypoint = waypoints_[next_++];
      std_msgs::msg::Float32 message;
      message.data = waypoint.value;
      publishers_.at(waypoint.topic)->publish(message);
    }
    if (next_ == waypoints_.size()) {
      next_ = 0;
      loop_start_ = Clock::now();
    }
    const double remaining = waypoints_[next_].seconds -
                             std::chrono::duration<double>(Clock::now() - loop_start_).count();
    const auto delay = std::max(
        std::chrono::nanoseconds(1),
        std::chrono::ceil<std::chrono::nanoseconds>(std::chrono::duration<double>(remaining)));
    // A short callback lets the executor process shutdown between waypoints.
    timer_ = create_wall_timer(delay, [this]() { PublishDueWaypoints(); });
  }

  std::vector<Waypoint> waypoints_;
  std::map<std::string, rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr> publishers_;
  rclcpp::TimerBase::SharedPtr timer_;
  Clock::time_point loop_start_;
  size_t next_ = 0;
};

#ifndef JOSHUA_NODE_TEST
int main(int argc, char* argv[]) {
  try {
    return ros2_utils::RunNode<TrajectoryPublisher>(argc, argv, "trajectory_publisher");
  } catch (const std::exception& error) {
    RCLCPP_ERROR(rclcpp::get_logger("trajectory_publisher"), "%s", error.what());
    if (rclcpp::ok()) rclcpp::shutdown();
    return 1;
  }
}
#else
std::shared_ptr<rclcpp::Node> MakeTrajectoryPublisher(const std::string& name,
                                                      int node_id,
                                                      const config::Config& config) {
  return std::make_shared<TrajectoryPublisher>(name, node_id, config);
}
#endif
