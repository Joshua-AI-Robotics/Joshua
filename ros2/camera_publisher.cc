#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "config/proto/robot.pb.h"
#include "config/config_utils.h"
#include <memory>
#include <sstream>
#include <iomanip>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/mat.hpp>

class CameraPublisher : public rclcpp::Node {
public:
  CameraPublisher(const std::string& node_name, const std::string& topic_name, const config::Config& config, int node_id)
  : Node(node_name) {
    robot::perception::PerceptionFactory perception_factory;

    for (const auto& single_perception : config.robot().perceptions().single_perceptions()) {
      if (single_perception.perception_type() == robot::perception::PerceptionType::CAMERA && 
          single_perception.node_id() == node_id) {
        cameras_.push_back(perception_factory.CreatePerception(single_perception));
        RCLCPP_INFO(this->get_logger(), "Found camera '%s' in configuration for node_id %d", 
                   single_perception.camera().camera_name().c_str(), node_id);
      }
    }
    
    if (cameras_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No camera found in configuration for node_id %d!", node_id);
      return;
    }
    
    // Create publisher for sensor_msgs Image
    image_publisher_ = this->create_publisher<sensor_msgs::msg::Image>(topic_name, 10);
    
    // Timer for publishing at specified rate
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(33), // ~30 Hz
      std::bind(&CameraPublisher::publish_camera_data, this));
      
    RCLCPP_INFO(this->get_logger(), "Camera publisher node started with %zu cameras for node_id %d!", 
               cameras_.size(), node_id);
    RCLCPP_INFO(this->get_logger(), "Publishing on topic: %s", topic_name.c_str());
  }

private:
  void publish_camera_data() {
    if (cameras_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "Camera not initialized!");
      return;
    }
    
    try {
      for (const auto& camera : cameras_) {
        auto data = camera->GetData();
        if (!data.has_value()) {
            RCLCPP_WARN(this->get_logger(), "Failed to get camera data!");
            return;
        }

        cv::Mat frame = std::any_cast<cv::Mat>(data);
        if (frame.empty()) {
            RCLCPP_WARN(this->get_logger(), "Empty image data received from camera!");
            return;
        }

        // Convert BGR to RGB (OpenCV uses BGR, ROS2 typically expects RGB)
        cv::Mat rgb_frame;
        cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);
        
        // Create sensor_msgs Image message
        auto image_msg = std::make_unique<sensor_msgs::msg::Image>();
        image_msg->header.stamp = this->now();
        image_msg->header.frame_id = "camera_frame";
        image_msg->height = rgb_frame.rows;
        image_msg->width = rgb_frame.cols;
        image_msg->encoding = "rgb8";
        image_msg->is_bigendian = false;
        image_msg->step = rgb_frame.cols * 3; // 3 bytes per pixel for RGB
        
        // Copy image data - ensure we copy the correct amount of data
        size_t data_size = rgb_frame.total() * rgb_frame.elemSize();
        image_msg->data.resize(data_size);
        std::memcpy(image_msg->data.data(), rgb_frame.data, data_size);
        
        image_publisher_->publish(*image_msg);
      }
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error publishing camera data: %s", e.what());
    }
  }
  
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::vector<std::unique_ptr<robot::perception::PerceptionInterface>> cameras_;
};

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  
  if (argc < 4) {
    RCLCPP_ERROR(rclcpp::get_logger("camera_publisher"), 
                 "Usage: camera_publisher <config_path> <node_id> <node_name>");
    return 1;
  }
  
  std::string config_path = argv[1];
  int node_id = std::stoi(argv[2]);
  std::string node_name = argv[3];
  
  config::Config config = config::config_util::LoadConfig(config_path);
  
  rclcpp::spin(std::make_shared<CameraPublisher>(node_name, "camera_image", config, node_id));
  rclcpp::shutdown();
  return 0;
} 