#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "config/proto/robot.pb.h"
#include "config/config_utils.h"
#include <memory>
#include <sstream>
#include <iomanip>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/mat.hpp>

class CameraPublisher : public rclcpp::Node {
private:
  struct Camera {
    std::string topic;
    std::unique_ptr<robot::perception::PerceptionInterface> interface;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher;
  };

public:
  CameraPublisher(const std::string& node_name, const int node_id, const config::Config& config)
  : Node(node_name) {
    robot::perception::PerceptionFactory perception_factory;

    for (const auto& single_perception : config.robot().perceptions().single_perceptions()) {
      if (single_perception.perception_type() == robot::perception::PerceptionType::CAMERA && 
          single_perception.node_id() == node_id) {
        const auto& camera_proto = single_perception.camera();

        auto interface = perception_factory.CreatePerception(single_perception);  
        if (!interface) {
            RCLCPP_ERROR(this->get_logger(), "Failed to create perception interface for camera '%s'. Check hardware connection or permissions.", 
                         camera_proto.camera_name().c_str());
            continue; // Skip this camera if initialization failed.
        }

        cameras_.emplace_back(Camera{
          .topic = single_perception.publish_topic(),
          .interface = std::move(interface),
          .publisher = this->create_publisher<sensor_msgs::msg::Image>(single_perception.publish_topic(), 10)
        });

        RCLCPP_INFO(this->get_logger(), "Found camera '%s' in configuration for node_id %d. Publishing on topic: %s", 
                    camera_proto.camera_name().c_str(), node_id, single_perception.publish_topic().c_str());
      }
    }
    
    if (cameras_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No camera found in configuration for node_id %d!", node_id);
      return;
    }
    
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(33), // ~30 Hz
      std::bind(&CameraPublisher::publish_camera_data, this));
      
    RCLCPP_INFO(this->get_logger(), "Camera publisher node started with %zu cameras for node_id %d!", 
               cameras_.size(), node_id);
  }

private:
  void publish_camera_data() {
    if (cameras_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "Camera not initialized!");
      return;
    }
    
    try {
      for (const auto& camera : cameras_) {
        RCLCPP_DEBUG(this->get_logger(), "Getting data from camera...");
        auto packet = camera.interface->GetData();
        
        // Check if packet contains image data
        if (!packet.has_image()) {
            RCLCPP_WARN(this->get_logger(), "Failed to get image data from camera!");
            continue;
        }

        const auto& image_data = packet.image();
        RCLCPP_DEBUG(this->get_logger(), "Image data: %dx%d, channels=%d, encoding=%s, data_size=%zu", 
                    image_data.width(), image_data.height(), image_data.channels(), 
                    image_data.encoding().c_str(), image_data.data().size());
        
        // Verify image data is valid
        if (image_data.width() <= 0 || image_data.height() <= 0 || image_data.channels() <= 0) {
            RCLCPP_ERROR(this->get_logger(), "Invalid image dimensions: %dx%d, channels=%d", 
                        image_data.width(), image_data.height(), image_data.channels());
            continue;
        }
        
        // Convert protobuf ImageData to cv::Mat
        cv::Mat frame;
        int cv_type;
        
        // Determine OpenCV type based on channels
        if (image_data.channels() == 1) {
            cv_type = CV_8UC1;  // Grayscale
        } else if (image_data.channels() == 3) {
            cv_type = CV_8UC3;  // RGB/BGR
        } else if (image_data.channels() == 4) {
            cv_type = CV_8UC4;  // RGBA/BGRA
        } else {
            RCLCPP_ERROR(this->get_logger(), "Unsupported number of channels: %d", image_data.channels());
            continue;
        }
        
        // Verify data size matches expected size
        size_t expected_size = image_data.height() * image_data.width() * image_data.channels();
        if (image_data.data().size() != expected_size) {
            RCLCPP_ERROR(this->get_logger(), "Image data size mismatch. Expected: %zu, Got: %zu", 
                        expected_size, image_data.data().size());
            continue;
        }
        
        RCLCPP_DEBUG(this->get_logger(), "Creating cv::Mat...");
        
        // Create cv::Mat using the safest possible approach
        frame = cv::Mat::zeros(image_data.height(), image_data.width(), cv_type);
        
        // Copy data byte by byte to be absolutely safe
        const std::string& data_str = image_data.data();
        if (data_str.size() == expected_size) {
            std::memcpy(frame.data, data_str.data(), data_str.size());
        } else {
            RCLCPP_ERROR(this->get_logger(), "Data size mismatch during copy");
            continue;
        }
        
        if (frame.empty()) {
            RCLCPP_WARN(this->get_logger(), "Empty image data received from camera!");
            continue;
        }

        RCLCPP_DEBUG(this->get_logger(), "Converting color space...");
        
        // Convert to RGB if needed (check encoding from protobuf)
        cv::Mat rgb_frame;
        if (image_data.encoding() == "bgr8" && image_data.channels() == 3) {
            cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);
        } else if (image_data.encoding() == "rgb8") {
            rgb_frame = frame.clone();
        } else if (image_data.encoding() == "mono8" && image_data.channels() == 1) {
            cv::cvtColor(frame, rgb_frame, cv::COLOR_GRAY2RGB);
        } else {
            RCLCPP_WARN(this->get_logger(), "Unsupported encoding: %s, using as-is", image_data.encoding().c_str());
            rgb_frame = frame.clone();
        }
        
        RCLCPP_DEBUG(this->get_logger(), "Creating ROS message...");
        
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
        
        RCLCPP_DEBUG(this->get_logger(), "Publishing image...");
        camera.publisher->publish(*image_msg);
        RCLCPP_DEBUG(this->get_logger(), "Image published successfully");
      }
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error publishing camera data: %s", e.what());
    }
  }
  
  rclcpp::TimerBase::SharedPtr timer_;
  std::vector<Camera> cameras_;
};

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  
  if (argc < 4) {
    RCLCPP_ERROR(rclcpp::get_logger("camera_publisher"), 
                 "Usage: camera_publisher <node_name> <node_id> <config_path>");
    return 1;
  }
  
  std::string node_name = argv[1];
  int node_id = std::stoi(argv[2]);
  std::string config_path = argv[3];
  
  config::Config config = config::config_util::LoadConfig(config_path);
  
  rclcpp::spin(std::make_shared<CameraPublisher>(node_name, node_id, config));
  rclcpp::shutdown();
  return 0;
} 