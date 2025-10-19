#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"
#include "utils/xbox_controller/xbox_controller.h"
#include <csignal>

class XboxControllerPublisher : public rclcpp::Node {
public:
  XboxControllerPublisher() : Node("xbox_controller_publisher") {
    publisher_ = this->create_publisher<std_msgs::msg::Float32MultiArray>("xbox_controller_data", 10);
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(16), // 60Hz update rate
      std::bind(&XboxControllerPublisher::publish_message, this));
    
    // Initialize Xbox controller
    if (!xbox_controller_.Init()) {
      RCLCPP_ERROR(this->get_logger(), "Failed to initialize Xbox controller!");
    } else {
      RCLCPP_INFO(this->get_logger(), "Xbox controller publisher node started!");
      RCLCPP_INFO(this->get_logger(), "Publishing normalized Float32MultiArray with 19 elements:");
      RCLCPP_INFO(this->get_logger(), "  [0-3]: Joysticks [-1,1] (left_stick_x, left_stick_y, right_stick_x, right_stick_y)");
      RCLCPP_INFO(this->get_logger(), "  [4-5]: D-pad [-1,1] (dpad_x, dpad_y)");
      RCLCPP_INFO(this->get_logger(), "  [6-7]: Triggers [0,1] (left_trigger, right_trigger)");
      RCLCPP_INFO(this->get_logger(), "  [8-18]: Buttons [0,1] (a, b, x, y, left_bumper, right_bumper, start, back, left_stick_click, right_stick_click, guide)");
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
    
    // Create Float32MultiArray message
    auto message = std_msgs::msg::Float32MultiArray();
    message.data.reserve(19); // 8 axes + 11 buttons
    
    // Normalization constants (from xbox_controller.h)
    const float JOYSTICK_RANGE = 32768.0f;  // -32768 to 32767
    const float TRIGGER_MAX = 1023.0f;      // 0 to 1023
    
    // Add normalized axes data (8 elements)
    // Joysticks: normalize from [-32768, 32767] to [-1, 1]
    message.data.push_back(static_cast<float>(controller_state_.abs_x_value) / JOYSTICK_RANGE);      // left_stick_x
    message.data.push_back(static_cast<float>(controller_state_.abs_y_value) / JOYSTICK_RANGE);      // left_stick_y
    message.data.push_back(static_cast<float>(controller_state_.abs_rx_value) / JOYSTICK_RANGE);     // right_stick_x
    message.data.push_back(static_cast<float>(controller_state_.abs_ry_value) / JOYSTICK_RANGE);     // right_stick_y
    
    // D-pad: already in [-1, 0, 1] range, just convert to float
    message.data.push_back(static_cast<float>(controller_state_.abs_hat0x_value));                  // dpad_x
    message.data.push_back(static_cast<float>(controller_state_.abs_hat0y_value));                  // dpad_y
    
    // Triggers: normalize from [0, 1023] to [0, 1]
    message.data.push_back(static_cast<float>(controller_state_.abs_z_value) / TRIGGER_MAX);        // left_trigger
    message.data.push_back(static_cast<float>(controller_state_.abs_rz_value) / TRIGGER_MAX);       // right_trigger
    
    // Add button data (11 elements) - already 0 or 1
    message.data.push_back(static_cast<float>(controller_state_.btn_south_state));  // a
    message.data.push_back(static_cast<float>(controller_state_.btn_east_state));   // b
    message.data.push_back(static_cast<float>(controller_state_.btn_west_state));   // x
    message.data.push_back(static_cast<float>(controller_state_.btn_north_state));  // y
    message.data.push_back(static_cast<float>(controller_state_.btn_tl_state));     // left_bumper
    message.data.push_back(static_cast<float>(controller_state_.btn_tr_state));     // right_bumper
    message.data.push_back(static_cast<float>(controller_state_.btn_start_state));  // start
    message.data.push_back(static_cast<float>(controller_state_.btn_select_state)); // back
    message.data.push_back(static_cast<float>(controller_state_.btn_thumbl_state)); // left_stick_click
    message.data.push_back(static_cast<float>(controller_state_.btn_thumbr_state)); // right_stick_click
    message.data.push_back(static_cast<float>(controller_state_.btn_mode_state));   // guide
    
    publisher_->publish(message);
  }
  
  rclcpp::Publisher<std_msgs::msg::Float32MultiArray>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  utils::XboxController xbox_controller_;
  utils::XboxControllerState controller_state_;
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  
  auto node = std::make_shared<XboxControllerPublisher>();
  
  // Set up signal handler for teardown
  std::signal(SIGINT, [](int) {
    RCLCPP_INFO(rclcpp::get_logger("xbox_controller_publisher"), "Received interrupt signal, shutting down...");
    rclcpp::shutdown();
  });
  
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
} 