#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "config/proto/config.pb.h"
#include "sensor_msgs/msg/image.hpp"
 
#include <algorithm>
#include <mutex>
#include <random>
#include <string>
#include <vector>
#include <numeric>
#include "ros2/node_runner.h"

class MockInference : public rclcpp::Node {
public:
  MockInference(const std::string& node_name, const int node_id, const config::Config& config)
  : Node(node_name) {
    if (config.general().operation_mode() != config::General::MODE_MOCK_INFERENCE) {
      RCLCPP_ERROR(this->get_logger(), "Mock inference node is only supported in mock inference mode.");
      return;
    }

    /*
    TODO
    1. Define the state(input) size. Currently set as subscribe_topics_size().
    2. Define the action size. Currently set as publish_topics_size().
    3. Define subscibe data type for subscriptions_. Currently hardcoded as sensor_msgs::msg::Image.
    4. Add check for size, topics, and so on.
    */

    // Initialize publishers.
    for (size_t i = 0; i < config.ai().publish_topics_size(); ++i) {
      auto topic = config.ai().publish_topics(i);
      publishers_.push_back(this->create_publisher<std_msgs::msg::Float32>(topic, 10));
    }

    // Initialize state tracking
    latest_states_.assign(config.ai().subscribe_topics_size(), 0.0f);
    received_.assign(config.ai().subscribe_topics_size(), false);

    // Random noise generator (example action output)
    std::random_device rd;
    random_generator_ = std::mt19937(rd());
    distribution_ = std::uniform_real_distribution<float>(-0.02f, 0.02f);

    // Subscriptions for all configured topics (images)
    for (size_t i = 0; i < config.ai().subscribe_topics_size(); ++i) {
      auto topic = config.ai().subscribe_topics(i);
      // TODO: Message type should not be hardcoded.
      subscriptions_.push_back(this->create_subscription<sensor_msgs::msg::Image>(
        topic, 10,
        [this, i](const sensor_msgs::msg::Image::SharedPtr msg) { on_image_data(i, msg); }
      ));
      RCLCPP_INFO(this->get_logger(), "Subscribed to image state topic: %s", topic.c_str());
    }

    RCLCPP_INFO(this->get_logger(), "Mock inference node started (%zu actions, %zu states).",
                config.ai().publish_topics_size(), config.ai().subscribe_topics_size());
  }

private:
  void on_image_data(size_t state_index, const sensor_msgs::msg::Image::SharedPtr msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_index >= latest_states_.size()) {
      RCLCPP_WARN(this->get_logger(), "Received state index out of range: %zu", state_index);
      return;
    }

    // Generate a random state value in from distribution.
    latest_states_[state_index] = distribution_(random_generator_);
    received_[state_index] = true;

    // If we have a fresh message from all topics, publish once.
    if (std::all_of(received_.begin(), received_.end(), [](bool v) { return v; })) {
      publish_actions_locked();
      std::fill(received_.begin(), received_.end(), false);
    }
  }

  void publish_actions_locked() {
    // Example: produce one action per publisher using simple transformation + small noise
    const float aggregated_state =
      std::accumulate(latest_states_.begin(), latest_states_.end(), 0.0f) /
      std::max<size_t>(1, latest_states_.size());

    for (auto& publisher : publishers_) {
      std_msgs::msg::Float32 msg;
      msg.data = aggregated_state + distribution_(random_generator_);
      publisher->publish(msg);
    }
  }

  std::vector<rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr> publishers_;
  std::vector<rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr> subscriptions_;

  std::vector<float> latest_states_;
  std::vector<bool> received_;

  std::mutex mutex_;
  std::mt19937 random_generator_;
  std::uniform_real_distribution<float> distribution_;
};

int main(int argc, char * argv[]) {
  return ros2_utils::RunNode<MockInference>(argc, argv, "mock_inference");
}
