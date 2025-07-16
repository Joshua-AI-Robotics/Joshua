#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/byte_multi_array.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "config/proto/robot.pb.h"
#include "config/config_utils.h"
#include <memory>
#include <sstream>
#include <iomanip>

class CameraPublisher : public rclcpp::Node {
public:
  CameraPublisher() : Node("camera_publisher") {
    // Load configuration
    config::Config config = config::config_util::LoadConfig("config/config_preset/so100_with_example_ai.pbtxt");
    
    robot::perception::PerceptionFactory perception_factory;
    
    // Find camera in configuration
    for (const auto& single_perception : config.robot().perceptions().single_perception()) {
      const auto& sensor_proto = single_perception.sensor();
      if (sensor_proto.sensor_type() == robot::perception::SensorType::CAMERA) {
        camera_ = perception_factory.CreatePerception(sensor_proto);
        RCLCPP_INFO(this->get_logger(), "Found camera in configuration");
        break;
      }
    }
    
    if (!camera_) {
      RCLCPP_ERROR(this->get_logger(), "No camera found in configuration!");
      return;
    }
    
    // Create publisher for raw image data as bytes
    image_publisher_ = this->create_publisher<std_msgs::msg::ByteMultiArray>("camera/image_bytes", 10);
    
    // Timer for publishing at specified rate
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(33), // ~30 Hz
      std::bind(&CameraPublisher::publish_camera_data, this));
      
    RCLCPP_INFO(this->get_logger(), "Camera publisher node started!");
  }

private:
  void publish_camera_data() {
    if (!camera_) {
      RCLCPP_ERROR(this->get_logger(), "Camera not initialized!");
      return;
    }
    
    try {
      auto packet = camera_->GetData();
      if (!packet) {
        RCLCPP_WARN(this->get_logger(), "Failed to get camera data!");
        return;
      }
      
      // Extract image data from your custom protobuf format
      const auto& image_data = packet->camera_perception().image_data();
      
      auto now = this->now();
      
      // Publish raw image data as bytes (matching your protobuf definition)
      auto image_msg = std::make_unique<std_msgs::msg::ByteMultiArray>();
      // Copy protobuf bytes directly to ByteMultiArray
      image_msg->data.assign(image_data.begin(), image_data.end());
      image_publisher_->publish(*image_msg);
      
      RCLCPP_DEBUG(this->get_logger(), "Published camera image data: %zu bytes", image_data.size());
      
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error publishing camera data: %s", e.what());
    }
  }
  
  rclcpp::Publisher<std_msgs::msg::ByteMultiArray>::SharedPtr image_publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::unique_ptr<robot::perception::PerceptionInterface> camera_;
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CameraPublisher>());
  rclcpp::shutdown();
  return 0;
} 