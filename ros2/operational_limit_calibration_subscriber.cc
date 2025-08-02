#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "config/config_utils.h"
#include <list>
#include <cfloat>

// TODO: Maybe rename to operational_limit_calibration.
class OperationalLimitCalibrationSubscriber : public rclcpp::Node {
private:
    struct OperationalLimit {
        std::string encoder_name;
        float min_value = FLT_MAX;
        float max_value = -FLT_MAX;
        rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr subscription;
    };

public:
    OperationalLimitCalibrationSubscriber(const std::string& node_name, const int node_id, const config::Config& config) : Node(node_name) {

        for(const auto& single_perception : config.robot().perceptions().single_perceptions()) {
            if(single_perception.perception_type() == robot::perception::PerceptionType::ENCODER && single_perception.node_id() == 2) {
                
                OperationalLimit& operational_limit = operational_limits_.emplace_back(OperationalLimit{
                    single_perception.encoder().encoder_name(),
                });

                auto callback = [this, &operational_limit](const std_msgs::msg::Float32::SharedPtr msg) {
                    operational_limit.max_value = std::max(operational_limit.max_value, msg->data);
                    operational_limit.min_value = std::min(operational_limit.min_value, msg->data);
                    RCLCPP_INFO(this->get_logger(), "Encoder: %s [%f %f %f] (min, current, max)", operational_limit.encoder_name.c_str(),
                     operational_limit.min_value, msg->data, operational_limit.max_value);
                };

                operational_limit.subscription = this->create_subscription<std_msgs::msg::Float32>(single_perception.publish_topic(), 10, callback);                
            }
        }

        if (operational_limits_.empty()) {
            RCLCPP_ERROR(this->get_logger(), "No encoder perceptions found in configuration for node_id %d!", node_id);
            return;
        }
    }

    ~OperationalLimitCalibrationSubscriber() {
        RCLCPP_INFO(this->get_logger(), "Operational Limit Result");
        for(auto& operational_limit : operational_limits_) {
            RCLCPP_INFO(this->get_logger(), "\t%s [%f %f]\t(min, max)", operational_limit.encoder_name.c_str(),
             operational_limit.min_value, operational_limit.max_value);
        }
    }

private:
    std::list<OperationalLimit> operational_limits_;
};

int main(int argc, char *argv[]) {
    // For test run:
    // bazel run ros2:encoder_calibration_subscriber -- --node_id="1" --node_name="test_calib" --config_path="config/config_preset/publish_two_so100_encoder_test.pbtxt"
    rclcpp::init(argc, argv);

    if (argc < 4) {
        RCLCPP_ERROR(rclcpp::get_logger("operational_limit_calibration_subscriber"), 
                     "Usage: operational_limit_calibration_subscriber <node_id> <node_name> <config_path>");
        return 1;
    }

    std::string node_name = argv[1];
    int node_id = std::stoi(argv[2]);    
    std::string config_path = argv[3];

    config::Config config = config::config_util::LoadConfig(config_path);

    auto node = std::make_shared<OperationalLimitCalibrationSubscriber>(node_name, node_id, config);
    rclcpp::spin(node);
    
    rclcpp::shutdown();
    return 0;
}