#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

#include "config/proto/config.pb.h"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/node_runner.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/u_int8_multi_array.hpp"

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
      source->interface = std::move(interface.value());
      const auto status = source->interface->Init();
      if (!status.ok()) throw std::runtime_error(std::string(status.message()));
      for (const auto& pub : sensor.node().publishers()) {
        source->publishers.push_back(create_publisher<std_msgs::msg::UInt8MultiArray>(
            pub.topic(), ros2_utils::CreateQosSetting(sensor.node().qos_setting())));
      }
      sources_.push_back(source);
      // Nonblocking polling drains available chunks without a second capture owner.
      const auto& audio = sensor.alsa_audio_config();
      const auto poll_us = std::max<uint64_t>(
          1000, static_cast<uint64_t>(audio.frames_per_chunk()) * 500000 / audio.sample_rate_hz());
      timers_.push_back(create_wall_timer(std::chrono::microseconds(poll_us),
                                          [this, source]() { Publish(source); }));
    }
    if (sources_.empty()) throw std::runtime_error("No audio source configured for node");
  }

 private:
  struct Source {
    std::unique_ptr<robot::perception::PerceptionInterface> interface;
    std::vector<rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr> publishers;
    std::chrono::steady_clock::time_point last_packet = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point retry_after{};
    std::chrono::steady_clock::time_point next_warning{};
  };
  void Publish(const std::shared_ptr<Source>& source) {
    const auto now = std::chrono::steady_clock::now();
    if (now < source->retry_after) return;
    // Bound executor work even if a plugin produces unlimited samples.
    for (int i = 0; i < 8; ++i) {
      auto packet = source->interface->GetData();
      if (!packet.ok()) {
        if (now - source->last_packet > std::chrono::seconds(2) && now >= source->next_warning) {
          RCLCPP_ERROR(get_logger(), "Audio unavailable: %s", packet.status().ToString().c_str());
          source->next_warning = now + std::chrono::seconds(1);
        }
        // A reopened microphone needs normal polling while its first chunk fills.
        // Delaying pending reads by a second would overrun the ALSA buffer again.
        if (!absl::IsDeadlineExceeded(packet.status())) {
          source->retry_after = now + std::chrono::seconds(1);
        }
        return;
      }
      source->last_packet = now;
      std_msgs::msg::UInt8MultiArray msg;
      const std::string bytes = packet->SerializeAsString();
      msg.data.assign(bytes.begin(), bytes.end());
      for (const auto& publisher : source->publishers) publisher->publish(msg);
    }
  }
  std::vector<std::shared_ptr<Source>> sources_;
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;
};
int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<AudioPublisher>(argc, argv, "audio_publisher");
}
