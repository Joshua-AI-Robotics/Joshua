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

class CameraPublisher : public rclcpp::Node {
public:
  CameraPublisher(const std::string& topic_name, const config::Config& config) : Node("camera_publisher") {
    robot::perception::PerceptionFactory perception_factory;
    
    // Find camera in configuration
    for (const auto& single_perception : config.robot().perceptions().single_perception()) {
      const auto& sensor_proto = single_perception.sensor();
      if (sensor_proto.sensor_type() == robot::perception::SensorType::CAMERA) {
        cameras_.push_back(perception_factory.CreatePerception(sensor_proto));
        RCLCPP_INFO(this->get_logger(), "Found camera in configuration");
        break;
      }
    }
    
    if (cameras_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No camera found in configuration!");
      return;
    }
    
    // Create publisher for sensor_msgs Image
    image_publisher_ = this->create_publisher<sensor_msgs::msg::Image>(topic_name, 10);
    
    // Timer for publishing at specified rate
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(33), // ~30 Hz
      std::bind(&CameraPublisher::publish_camera_data, this));
      
    RCLCPP_INFO(this->get_logger(), "Camera publisher node started!");
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
        auto packet = camera->GetData();
      if (!packet) {
        RCLCPP_WARN(this->get_logger(), "Failed to get camera data!");
        return;
      }
      
      // Extract image data from your custom protobuf format
      const auto& image_data = packet->camera_perception().image_data();
      
      if (image_data.empty()) {
        RCLCPP_WARN(this->get_logger(), "Empty image data received from camera!");
        return;
      }
      
      // Convert JPEG bytes to OpenCV Mat
      std::vector<uchar> buffer(image_data.begin(), image_data.end());
      cv::Mat frame = cv::imdecode(buffer, cv::IMREAD_COLOR);
      
      if (frame.empty()) {
        RCLCPP_WARN(this->get_logger(), "Failed to decode image from camera data!");
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

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  config::Config config = config::config_util::LoadConfig(argv[1]);
  
  //TODO: Update the subscription topic based on the operation mode and config.
  rclcpp::spin(std::make_shared<CameraPublisher>("camera/image_raw", config));
  rclcpp::shutdown();
  return 0;
} 