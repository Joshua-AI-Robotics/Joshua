#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/perception/interfaces/perception_interface.h"
#include "robot/perception/proto/perception.pb.h"
#include "sensor_msgs/msg/image.hpp"

class CameraPublisher : public rclcpp::Node {
 public:
  using Clock = std::chrono::steady_clock;
  using Now = std::function<Clock::time_point()>;
  using Factory =
      std::function<absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>>(
          const robot::perception::SinglePerception&)>;

  CameraPublisher(const std::string& node_name, int node_id, const config::Config& config);
  // Inject the source and retry clock for hardware-free lifecycle tests.
  CameraPublisher(const std::string& node_name,
                  int node_id,
                  const config::Config& config,
                  Factory factory,
                  Now now);

 private:
  // Initialization and recovery belong to the physical source, shared by all
  // its publishers. An unavailable source remains registered for later retries.
  struct Source {
    std::string name;
    std::unique_ptr<robot::perception::PerceptionInterface> interface;
    bool initialized = false;
    Clock::time_point retry_after{};
  };
  struct Camera {
    std::string topic;
    std::shared_ptr<Source> source;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher;
    rclcpp::TimerBase::SharedPtr timer;
  };

  bool InitializeCamera(Source& source);
  void ResetCamera(Source& source);
  void PublishCameraData();

  Now now_;
  std::vector<Camera> cameras_;
};
