#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "config/config_utils.h"
#include "config/proto/robot.pb.h"
#include "config/proto/ai.pb.h"
#include "sensor_msgs/msg/image.hpp"

#include <algorithm>
#include <mutex>
#include <random>
#include <string>
#include <vector>
#include <numeric>

class MockInference : public rclcpp::Node {
public:
  MockInference(const std::string& node_name, const int node_id, const config::Config& config)
  : Node(node_name) {
    if (config.ai().ai_mode() != config::AiMode::MODE_MOCK_INFERENCE) {
      RCLCPP_ERROR(this->get_logger(), "Mock inference node is only supported in mock inference mode.");
      return;
    }

    action_size_ = static_cast<size_t>(config.robot().actions().single_actions().size());
    // TODO: This should be updated for multi-modal states, but for now align state count with subscribed topics.
    subscribe_topics_.assign(config.ai().subscribe_topics().begin(), config.ai().subscribe_topics().end());
    subscribe_size_ = subscribe_topics_.size();
    state_size_ = subscribe_size_;
    
    // Initialize publishers.
    for (size_t i = 1; i <= action_size_; ++i) {
      // TODO: Topic should not be hardcoded.
      auto topic = "mock_action_" + std::to_string(i);
      publishers_.push_back(this->create_publisher<std_msgs::msg::Float32>(topic, 10));
    }

    // Initialize state tracking
    latest_states_.assign(state_size_, 0.0f);
    received_.assign(state_size_, false);

    // Random noise generator (example action output)
    std::random_device rd;
    random_generator_ = std::mt19937(rd());
    distribution_ = std::uniform_real_distribution<float>(-0.02f, 0.02f);

    // Subscriptions for all configured topics (images)
    for (size_t i = 0; i < subscribe_size_; ++i) {
      auto topic = subscribe_topics_[i];
      // TODO: Message type should not be hardcoded.
      subscriptions_.push_back(this->create_subscription<sensor_msgs::msg::Image>(
        topic, 10,
        [this, i](const sensor_msgs::msg::Image::SharedPtr msg) { on_image_data(i, msg); }
      ));
      RCLCPP_INFO(this->get_logger(), "Subscribed to image state topic: %s", topic.c_str());
    }

    RCLCPP_INFO(this->get_logger(), "Mock inference node started (%zu actions, %zu states).",
                action_size_, state_size_);
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

  size_t action_size_{0};
  size_t state_size_{0};
  size_t subscribe_size_{0};
  std::vector<std::string> subscribe_topics_;

  std::vector<rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr> publishers_;
  std::vector<rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr> subscriptions_;

  std::vector<float> latest_states_;
  std::vector<bool> received_;

  std::mutex mutex_;

  std::mt19937 random_generator_;
  std::uniform_real_distribution<float> distribution_;
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);

  if (argc < 4) {
    RCLCPP_ERROR(rclcpp::get_logger("mock_inference"), 
                 "Usage: mock_inference <node_name> <node_id> <config_path>");
    return 1;
  }
  
  std::string node_name = argv[1];
  int node_id = std::stoi(argv[2]);
  std::string config_path = argv[3];
  
  config::Config config = config::config_util::LoadConfig(config_path);

  rclcpp::spin(std::make_shared<MockInference>(node_name, node_id, config));
  rclcpp::shutdown();
  return 0;
}
