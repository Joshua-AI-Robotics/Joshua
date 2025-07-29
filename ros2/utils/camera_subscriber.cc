#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <memory>

class CameraSubscriber : public rclcpp::Node {
public:
  CameraSubscriber() : Node("camera_subscriber") {
    // Create subscriber for camera image data
    subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
      "camera/image_raw", 10, std::bind(&CameraSubscriber::image_callback, this, std::placeholders::_1));
    
    // Create OpenCV window
    cv::namedWindow("Camera Feed", cv::WINDOW_AUTOSIZE);
    
    RCLCPP_INFO(this->get_logger(), "Camera subscriber node started!");
    RCLCPP_INFO(this->get_logger(), "Listening on topic: /camera/image_raw");
    RCLCPP_INFO(this->get_logger(), "Press 'q' to quit");
  }
  
  ~CameraSubscriber() {
    cv::destroyAllWindows();
    RCLCPP_INFO(this->get_logger(), "Camera subscriber node shutting down.");
  }

private:
  void image_callback(const sensor_msgs::msg::Image::SharedPtr msg) {
    try {
      // Validate message
      if (msg->data.empty()) {
        RCLCPP_WARN(this->get_logger(), "Received empty image data!");
        return;
      }
      
      // Check encoding
      if (msg->encoding != "rgb8") {
        RCLCPP_WARN(this->get_logger(), "Unexpected encoding: %s, expected rgb8", msg->encoding.c_str());
        return;
      }
      
      // Create OpenCV Mat from sensor_msgs Image
      cv::Mat frame(msg->height, msg->width, CV_8UC3);
      
      // Copy image data
      if (msg->data.size() != frame.total() * frame.elemSize()) {
        RCLCPP_ERROR(this->get_logger(), "Data size mismatch! Expected: %zu, Got: %zu", 
                     frame.total() * frame.elemSize(), msg->data.size());
        return;
      }
      
      std::memcpy(frame.data, msg->data.data(), msg->data.size());
      
      // Convert RGB to BGR for OpenCV display (since we received as RGB)
      cv::Mat bgr_frame;
      cv::cvtColor(frame, bgr_frame, cv::COLOR_RGB2BGR);
      
      if (bgr_frame.empty()) {
        RCLCPP_WARN(this->get_logger(), "Failed to process image from received data!");
        return;
      }
      
      // Create a copy for display with overlay
      cv::Mat display_frame = bgr_frame.clone();
      
      // Add info text to the frame
      std::string info = "Size: " + std::to_string(bgr_frame.cols) + "x" + std::to_string(bgr_frame.rows);
      cv::putText(display_frame, info, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
      
      // Add timestamp info
      std::string timestamp = "Time: " + std::to_string(msg->header.stamp.sec) + "." + 
                             std::to_string(msg->header.stamp.nanosec);
      cv::putText(display_frame, timestamp, cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 0), 2);
      
      // Display the frame
      cv::imshow("Camera Feed", display_frame);
      
      RCLCPP_DEBUG(this->get_logger(), "Received image: %dx%d, %zu bytes, encoding: %s", 
                   bgr_frame.cols, bgr_frame.rows, msg->data.size(), msg->encoding.c_str());
      
      // Check for 'q' key to quit
      int key = cv::waitKey(1);
      if (key == 'q' || key == 27) { // 'q' or ESC key
        RCLCPP_INFO(this->get_logger(), "Quit key pressed, shutting down...");
        rclcpp::shutdown();
      }
      
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error processing image: %s", e.what());
    }
  }
  
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CameraSubscriber>());
  rclcpp::shutdown();
  return 0;
} 