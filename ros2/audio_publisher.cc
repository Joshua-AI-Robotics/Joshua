#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

#include "config/proto/config.pb.h"
#include "robot/perception/audio/audio_capture.h"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/node_runner.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/u_int8_multi_array.hpp"

namespace {
// Retry device failures without busy-looping; pending chunks keep normal polling.
constexpr auto kCaptureRetryBackoff = std::chrono::seconds(1);
// Report sustained outages, then throttle repeated warnings.
constexpr auto kCaptureStallWarningDelay = std::chrono::seconds(2);
constexpr auto kCaptureWarningInterval = std::chrono::seconds(1);
}  // namespace

class AudioPublisher : public rclcpp::Node {
 public:
  AudioPublisher(const std::string& name, int node_id, const config::Config& config) : Node(name) {
    for (const auto& sensor : config.robot().perceptions().single_perceptions()) {
      if (sensor.sensor_type() != robot::perception::AUDIO ||
          static_cast<int>(sensor.node().id()) != node_id)
        continue;
      auto interface =
          robot::perception::PerceptionFactory::CreatePerception(sensor, config.robot().boards());
      if (!interface.ok()) throw std::runtime_error(std::string(interface.status().message()));
      auto source = std::make_shared<Source>();
      auto* audio_interface = dynamic_cast<robot::perception::AudioInterface*>(interface->get());
      if (!audio_interface) throw std::runtime_error("Audio source requires AudioInterface");
      interface->release();
      source->interface.reset(audio_interface);
      const auto status = source->interface->Init();
      if (!status.ok()) throw std::runtime_error(std::string(status.message()));
      for (const auto& pub : sensor.node().publishers()) {
        source->publishers.push_back(create_publisher<std_msgs::msg::UInt8MultiArray>(
            pub.topic(), ros2_utils::CreateQosSetting(sensor.node().qos_setting())));
      }
      sources_.push_back(source);
      UpdateTimer(source);
    }
    if (sources_.empty()) throw std::runtime_error("No audio source configured for node");
  }

 private:
  struct Source {
    std::unique_ptr<robot::perception::AudioInterface> interface;
    std::vector<rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr> publishers;
    std::chrono::steady_clock::time_point last_packet = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point retry_after{};
    std::chrono::steady_clock::time_point next_warning{};
    std::chrono::nanoseconds poll_interval{};
    rclcpp::TimerBase::SharedPtr timer;
  };
  void Publish(const std::shared_ptr<Source>& source) {
    const auto now = std::chrono::steady_clock::now();
    if (now < source->retry_after) return;
    const auto status = robot::perception::DrainAudio(*source->interface, [&](const auto& packet) {
      source->last_packet = now;
      std_msgs::msg::UInt8MultiArray msg;
      const std::string bytes = packet.SerializeAsString();
      msg.data.assign(bytes.begin(), bytes.end());
      for (const auto& publisher : source->publishers) publisher->publish(msg);
    });
    // Reopening can negotiate a different buffer. Refresh even on pending reads
    // so the new capture session never inherits the old, potentially slow timer.
    UpdateTimer(source);
    if (!status.ok()) {
      if (now - source->last_packet > kCaptureStallWarningDelay && now >= source->next_warning) {
        RCLCPP_ERROR(get_logger(), "Audio unavailable: %s", status.ToString().c_str());
        source->next_warning = now + kCaptureWarningInterval;
      }
      if (!absl::IsDeadlineExceeded(status)) source->retry_after = now + kCaptureRetryBackoff;
    }
  }
  void UpdateTimer(const std::shared_ptr<Source>& source) {
    const auto interval = source->interface->GetCaptureSchedule().poll_interval;
    if (source->timer && source->poll_interval == interval) return;
    if (source->timer) source->timer->cancel();
    source->poll_interval = interval;
    // A weak capture avoids a Source -> timer -> Source ownership cycle.
    source->timer = create_wall_timer(interval, [this, weak = std::weak_ptr<Source>(source)]() {
      if (auto locked = weak.lock()) Publish(locked);
    });
  }
  std::vector<std::shared_ptr<Source>> sources_;
};
int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<AudioPublisher>(argc, argv, "audio_publisher");
}
