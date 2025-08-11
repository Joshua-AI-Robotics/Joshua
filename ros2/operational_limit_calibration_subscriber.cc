#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "config/proto/config.pb.h"
#include <algorithm>
#include <list>
#include <cfloat>
#include "ros2/node_runner.h"

// TODO: Maybe rename to operational_limit_calibration.
class OperationalLimitCalibrationSubscriber : public rclcpp::Node {
private:
    struct OperationalLimit {
        float min_value = FLT_MAX;
        float max_value = -FLT_MAX;
        std::string subscribe_topic;
        rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr subscription;
    };

public:
    OperationalLimitCalibrationSubscriber(const std::string& node_name, const int node_id, const config::Config& config) : Node(node_name) {

        for(const auto& subscribe_topic : config.calibration().subscribe_topics()) {
            operational_limits_.emplace_back();
            OperationalLimit& operational_limit = operational_limits_.back();
            operational_limit.subscribe_topic = subscribe_topic;

            auto callback = [this, &operational_limit](const std_msgs::msg::Float32::SharedPtr msg) {
                operational_limit.max_value = std::max(operational_limit.max_value, msg->data);
                operational_limit.min_value = std::min(operational_limit.min_value, msg->data);
                RCLCPP_INFO(this->get_logger(), "Encoder: %s [%f %f %f] (min, current, max)", operational_limit.subscribe_topic.c_str(),
                            operational_limit.min_value, msg->data, operational_limit.max_value);
            };

            operational_limit.subscription = this->create_subscription<std_msgs::msg::Float32>(operational_limit.subscribe_topic, 10, callback);
        }

        if (operational_limits_.empty()) {
            RCLCPP_ERROR(this->get_logger(), "No encoder perceptions found in configuration for node_id %d!", node_id);
            return;
        }
    }

    ~OperationalLimitCalibrationSubscriber() {
        RCLCPP_INFO(this->get_logger(), "Operational Limit Result");
        for(auto& operational_limit : operational_limits_) {
            RCLCPP_INFO(this->get_logger(), "\t%s [%f %f]\t(min, max)", operational_limit.subscribe_topic.c_str(),
             operational_limit.min_value, operational_limit.max_value);
        }
    }

private:
    std::list<OperationalLimit> operational_limits_;
};

int main(int argc, char *argv[]) {
    // For test run:
    // bazel run ros2:operational_limit_calibration_subscriber -- test_encoder 3 config/config_preset/calibrate_so100_operational_limit.pbtxt
    return ros2_utils::RunNode<OperationalLimitCalibrationSubscriber>(argc, argv, "operational_limit_calibration_subscriber");
}