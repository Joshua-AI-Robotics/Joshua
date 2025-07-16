#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "utils/xbox_controller/xbox_controller.h"
#include <sstream>
#include <iomanip>
#include <csignal>

class XboxControllerPublisher : public rclcpp::Node {
public:
  XboxControllerPublisher() : Node("xbox_controller_publisher") {
    publisher_ = this->create_publisher<std_msgs::msg::String>("xbox_controller_data", 10);
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100), // 10Hz update rate
      std::bind(&XboxControllerPublisher::publish_message, this));
    
    // Initialize Xbox controller
    if (!xbox_controller_.Init()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to initialize Xbox controller!");
    } else {
      RCLCPP_INFO(this->get_logger(), "Xbox controller publisher node started!");
    }
  }
  
  ~XboxControllerPublisher() {
    // Ensure proper cleanup when the node is destroyed
    xbox_controller_.Cleanup();
    RCLCPP_INFO(this->get_logger(), "Xbox controller publisher node shutting down.");
  }
  
private:
  void publish_message() {
    // Process any pending controller events
    xbox_controller_.ProcessEvents(controller_state_);
    
    // Create JSON-like string with controller data
    std::ostringstream oss;
    oss << "{";
    oss << "\"axes\":{";
    oss << "\"left_stick\":{\"x\":" << controller_state_.abs_x_value << ",\"y\":" << controller_state_.abs_y_value << "},";
    oss << "\"right_stick\":{\"x\":" << controller_state_.abs_rx_value << ",\"y\":" << controller_state_.abs_ry_value << "},";
    oss << "\"dpad\":{\"x\":" << controller_state_.abs_hat0x_value << ",\"y\":" << controller_state_.abs_hat0y_value << "},";
    oss << "\"triggers\":{\"left\":" << controller_state_.abs_z_value << ",\"right\":" << controller_state_.abs_rz_value << "}";
    oss << "},";
    oss << "\"buttons\":{";
    oss << "\"a\":" << controller_state_.btn_south_state << ",";
    oss << "\"b\":" << controller_state_.btn_east_state << ",";
    oss << "\"x\":" << controller_state_.btn_west_state << ",";
    oss << "\"y\":" << controller_state_.btn_north_state << ",";
    oss << "\"left_bumper\":" << controller_state_.btn_tl_state << ",";
    oss << "\"right_bumper\":" << controller_state_.btn_tr_state << ",";
    oss << "\"start\":" << controller_state_.btn_start_state << ",";
    oss << "\"back\":" << controller_state_.btn_select_state << ",";
    oss << "\"left_stick_click\":" << controller_state_.btn_thumbl_state << ",";
    oss << "\"right_stick_click\":" << controller_state_.btn_thumbr_state << ",";
    oss << "\"guide\":" << controller_state_.btn_mode_state;
    oss << "}";
    oss << "}";
    
    auto message = std_msgs::msg::String();
    message.data = oss.str();
    publisher_->publish(message);
    
    // Log some key values for debugging
    if (controller_state_.btn_start_state == 1) {
      RCLCPP_INFO(this->get_logger(), "Start button pressed - controller data: %s", message.data.c_str());
    }
  }
  
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  utils::XboxController xbox_controller_;
  utils::XboxControllerState controller_state_;
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  
  auto node = std::make_shared<XboxControllerPublisher>();
  
  // Set up signal handler for graceful shutdown
  std::signal(SIGINT, [](int) {
    RCLCPP_INFO(rclcpp::get_logger("xbox_controller_publisher"), "Received interrupt signal, shutting down...");
    rclcpp::shutdown();
  });
  
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
} 