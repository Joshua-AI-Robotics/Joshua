#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "config/config_utils.h"

// TODO: Maybe rename to operational_limit_calibration.
class CalibrationSubscriber : public rclcpp::Node {
public:
    CalibrationSubscriber(const std::string& node_name, const int node_id, const config::Config& config) : Node(node_name) {

        for(const auto& single_perception : config.robot().perceptions().single_perceptions()) {
            if(single_perception.perception_type() == robot::perception::PerceptionType::ENCODER) {
                auto callback = [this, &single_perception](const std_msgs::msg::Float32::SharedPtr msg) {
                    RCLCPP_INFO(this->get_logger(), "Encoder: %s Data: %f", single_perception.encoder().encoder_name().c_str(), msg->data);   
                    // TODO: Implement calibration logic here                 
                    //calibration_data_.push_back(std::make_pair(msg->data, msg->data));
                };
                subscriptions_.emplace_back(this->create_subscription<std_msgs::msg::Float32>(single_perception.publish_topic(), 10, callback));
            }
        }

        if (subscriptions_.empty()) {
            RCLCPP_ERROR(this->get_logger(), "No encoder perceptions found in configuration for node_id %d!", node_id);
            return;
        }
    }


private:
    std::vector<std::pair<float, float>> calibration_data_;
    std::vector<rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr> subscriptions_;
};

int main(int argc, char *argv[]) {
    // For test run:
    // bazel run ros2:calibration_subscriber -- --node_id="1" --node_name="test_calib" --config_path="config/config_preset/publish_two_so100_encoder_test.pbtxt"
    rclcpp::init(argc, argv);

    if (argc < 4) {
        RCLCPP_ERROR(rclcpp::get_logger("calibration_subscriber"), 
                     "Usage: calibration_subscriber <node_id> <node_name> <config_path>");
        return 1;
    }

    std::string node_name = argv[1];
    int node_id = std::stoi(argv[2]);    
    std::string config_path = argv[3];

    config::Config config = config::config_util::LoadConfig(config_path);

    auto node = std::make_shared<CalibrationSubscriber>(node_name, node_id, config);
    rclcpp::spin(node);
    
    rclcpp::shutdown();
    return 0;
}