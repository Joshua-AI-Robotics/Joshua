#include <glog/logging.h>

#include <memory>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

class CameraSubscriber : public rclcpp::Node {
 public:
  CameraSubscriber(const std::string& subscribe_topic) : Node("camera_subscriber") {
    // Create subscriber for camera image data
    subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
        subscribe_topic,
        10,
        std::bind(&CameraSubscriber::image_callback, this, std::placeholders::_1));

    // Create OpenCV window
    cv::namedWindow("Camera Feed", cv::WINDOW_AUTOSIZE);

    LOG(INFO) << "[" << get_name() << "] "
              << "Camera subscriber node started!";
    LOG(INFO) << "[" << get_name() << "] "
              << "Listening on topic: " << subscribe_topic;
    LOG(INFO) << "[" << get_name() << "] "
              << "Press 'q' to quit";
  }

  ~CameraSubscriber() {
    cv::destroyAllWindows();
    LOG(INFO) << "[" << get_name() << "] "
              << "Camera subscriber node shutting down.";
  }

 private:
  void image_callback(const sensor_msgs::msg::Image::SharedPtr msg) {
    try {
      // Validate message
      if (msg->data.empty()) {
        LOG(WARNING) << "[" << get_name() << "] "
                     << "Received empty image data!";
        return;
      }

      // Check encoding
      if (msg->encoding != "rgb8") {
        LOG(WARNING) << "[" << get_name() << "] "
                     << "Unexpected encoding: " << msg->encoding << ", expected rgb8";
        return;
      }

      // Create OpenCV Mat from sensor_msgs Image
      cv::Mat frame(msg->height, msg->width, CV_8UC3);

      // Copy image data
      if (msg->data.size() != frame.total() * frame.elemSize()) {
        LOG(ERROR) << "[" << get_name() << "] "
                   << "Data size mismatch! Expected: " << frame.total() * frame.elemSize()
                   << ", Got: " << msg->data.size();
        return;
      }

      std::memcpy(frame.data, msg->data.data(), msg->data.size());

      // Convert RGB to BGR for OpenCV display (since we received as RGB)
      cv::Mat bgr_frame;
      cv::cvtColor(frame, bgr_frame, cv::COLOR_RGB2BGR);

      if (bgr_frame.empty()) {
        LOG(WARNING) << "[" << get_name() << "] "
                     << "Failed to process image from received data!";
        return;
      }

      // Create a copy for display with overlay
      cv::Mat display_frame = bgr_frame.clone();

      // Add info text to the frame
      std::string info =
          "Size: " + std::to_string(bgr_frame.cols) + "x" + std::to_string(bgr_frame.rows);
      cv::putText(display_frame,
                  info,
                  cv::Point(10, 30),
                  cv::FONT_HERSHEY_SIMPLEX,
                  0.7,
                  cv::Scalar(0, 255, 0),
                  2);

      // Add timestamp info
      std::string timestamp = "Time: " + std::to_string(msg->header.stamp.sec) + "." +
                              std::to_string(msg->header.stamp.nanosec);
      cv::putText(display_frame,
                  timestamp,
                  cv::Point(10, 60),
                  cv::FONT_HERSHEY_SIMPLEX,
                  0.6,
                  cv::Scalar(255, 255, 0),
                  2);

      // Display the frame
      cv::imshow("Camera Feed", display_frame);

      VLOG(1) << "[" << get_name() << "] "
              << "Received image: " << bgr_frame.cols << "x" << bgr_frame.rows << ", "
              << msg->data.size() << " bytes, encoding: " << msg->encoding;

      // Check for 'q' key to quit
      int key = cv::waitKey(1);
      if (key == 'q' || key == 27) {  // 'q' or ESC key
        LOG(INFO) << "[" << get_name() << "] "
                  << "Quit key pressed, shutting down...";
        rclcpp::shutdown();
      }

    } catch (const std::exception& e) {
      LOG(ERROR) << "[" << get_name() << "] "
                 << "Error processing image: " << e.what();
    }
  }

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
};

// To run:
// docker compose run --rm joshua-u22 bazel run --config=u22 --config=x86-base
// ros2/utils:camera_subscriber <topic_name>
int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  rclcpp::init(argc, argv);

  if (argc < 2) {
    LOG(WARNING) << "No topic provided, using default topic: /camera_1";
    argv[1] = "/camera_1";
  }

  rclcpp::spin(std::make_shared<CameraSubscriber>(argv[1]));
  rclcpp::shutdown();
  return 0;
}
