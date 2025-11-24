#include <algorithm>
#include <cfloat>
#include <list>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "ros2/node_runner.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/float32.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"

// TODO: Maybe rename to operational_limit_calibration.
class OperationalLimitCalibration : public rclcpp::Node {
 private:
  struct OperationalLimit {
    float min_value = FLT_MAX;
    float max_value = -FLT_MAX;
    std::string subscribe_topic;
    std::string publish_topic;
    rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr subscription;
    rclcpp::Publisher<std_msgs::msg::Float32MultiArray>::SharedPtr publisher;
  };

 public:
  OperationalLimitCalibration(const std::string& node_name,
                              const int node_id,
                              const config::Config& config)
      : Node(node_name) {
    for (const auto& subscribe_topic : config.calibration().subscribe_topics()) {
      operational_limits_.emplace_back();
      OperationalLimit& operational_limit = operational_limits_.back();
      const auto& qos_setting = config.calibration().node().qos_setting();
      operational_limit.subscribe_topic = subscribe_topic;
      operational_limit.publish_topic = subscribe_topic + "_operational_limit";

      auto callback = [this, &operational_limit](const std_msgs::msg::Float32::SharedPtr msg) {
        operational_limit.max_value = std::max(operational_limit.max_value, msg->data);
        operational_limit.min_value = std::min(operational_limit.min_value, msg->data);

        std_msgs::msg::Float32MultiArray msg_array;
        msg_array.data.push_back(operational_limit.min_value);
        msg_array.data.push_back(operational_limit.max_value);
        operational_limit.publisher->publish(msg_array);
      };

      operational_limit.subscription = this->create_subscription<std_msgs::msg::Float32>(
          operational_limit.subscribe_topic, ros2_utils::CreateQosSetting(qos_setting), callback);
      operational_limit.publisher = this->create_publisher<std_msgs::msg::Float32MultiArray>(
          operational_limit.publish_topic, ros2_utils::CreateQosSetting(qos_setting));
    }

    if (operational_limits_.empty()) {
      RCLCPP_ERROR(this->get_logger(),
                   "No encoder perceptions found in configuration for node_id %d!",
                   node_id);
      return;
    }
  }

  ~OperationalLimitCalibration() {
    RCLCPP_INFO(this->get_logger(), "Operational Limit Calibration Subscriber Shutdown");
  }

 private:
  std::list<OperationalLimit> operational_limits_;
};

int main(int argc, char* argv[]) {
  // For test run:
  // bazel run ros2:operational_limit_calibration -- test_encoder 3
  // config/config_preset/calibrate_so100_operational_limit.pbtxt
  return ros2_utils::RunNode<OperationalLimitCalibration>(
      argc, argv, "operational_limit_calibration");
}
