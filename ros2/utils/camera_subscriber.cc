#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/byte_multi_array.hpp"
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <memory>

class CameraSubscriber : public rclcpp::Node {
public:
  CameraSubscriber() : Node("camera_subscriber") {
    // Create subscriber for camera image data
    subscription_ = this->create_subscription<std_msgs::msg::ByteMultiArray>(
      "camera/image_bytes", 10, std::bind(&CameraSubscriber::image_callback, this, std::placeholders::_1));
    
    // Create OpenCV window
    cv::namedWindow("Camera Feed", cv::WINDOW_AUTOSIZE);
    
    RCLCPP_INFO(this->get_logger(), "Camera subscriber node started!");
    RCLCPP_INFO(this->get_logger(), "Listening on topic: /camera/image_bytes");
    RCLCPP_INFO(this->get_logger(), "Press 'q' to quit");
  }
  
  ~CameraSubscriber() {
    cv::destroyAllWindows();
    RCLCPP_INFO(this->get_logger(), "Camera subscriber node shutting down.");
  }

private:
  void image_callback(const std_msgs::msg::ByteMultiArray::SharedPtr msg) {
    try {
      // Convert ByteMultiArray to vector<char> for OpenCV
      std::vector<char> data(msg->data.begin(), msg->data.end());
      
      // Decode image using OpenCV
      cv::Mat frame = cv::imdecode(data, cv::IMREAD_COLOR);
      
      if (frame.empty()) {
        RCLCPP_WARN(this->get_logger(), "Failed to decode image from received data!");
        return;
      }
      
      // Display the frame
      cv::imshow("Camera Feed", frame);
      
      // Add some info text to the frame
      std::string info = "Size: " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows) + 
                        " | Data: " + std::to_string(msg->data.size()) + " bytes";
      cv::putText(frame, info, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
      
      RCLCPP_DEBUG(this->get_logger(), "Received image: %dx%d, %zu bytes", 
                   frame.cols, frame.rows, msg->data.size());
      
      // Check for 'q' key to quit
      if (cv::waitKey(1) == 'q') { // This part is essential for the program to work.
        RCLCPP_INFO(this->get_logger(), "Quit key pressed, shutting down...");
        rclcpp::shutdown();
      }
      
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error processing image: %s", e.what());
    }
  }
  
  rclcpp::Subscription<std_msgs::msg::ByteMultiArray>::SharedPtr subscription_;
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CameraSubscriber>());
  rclcpp::shutdown();
  return 0;
} 