#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <atomic>

class KeyboardPublisher : public rclcpp::Node
{
public:
    KeyboardPublisher() : Node("keyboard_publisher")
    {
        publisher_ = this->create_publisher<std_msgs::msg::String>("keyboard_input", 10);
        
        // Set terminal to raw mode for immediate key detection
        tcgetattr(STDIN_FILENO, &old_tio_);
        new_tio_ = old_tio_;
        new_tio_.c_lflag &= (~ICANON & ~ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio_);
        
        RCLCPP_INFO(this->get_logger(), "Keyboard publisher started. Press W, A, S, D keys (or 'q' to quit)");
        
        // Start keyboard input thread
        running_ = true;
        keyboard_thread_ = std::thread(&KeyboardPublisher::keyboard_input_loop, this);
    }
    
    ~KeyboardPublisher()
    {
        // Stop the keyboard thread
        running_ = false;
        if (keyboard_thread_.joinable()) {
            keyboard_thread_.join();
        }
        
        // Restore terminal settings
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio_);
    }

private:
    void keyboard_input_loop()
    {
        while (running_ && rclcpp::ok())
        {
            char input;
            if (read(STDIN_FILENO, &input, 1) > 0)
            {
                std_msgs::msg::String msg;
                bool should_publish = false;
                
                switch (input)
                {
                    case 'w':
                    case 'W':
                        msg.data = "W";
                        RCLCPP_INFO(this->get_logger(), "W key pressed");
                        should_publish = true;
                        break;
                    case 'a':
                    case 'A':
                        msg.data = "A";
                        RCLCPP_INFO(this->get_logger(), "A key pressed");
                        should_publish = true;
                        break;
                    case 's':
                    case 'S':
                        msg.data = "S";
                        RCLCPP_INFO(this->get_logger(), "S key pressed");
                        should_publish = true;
                        break;
                    case 'd':
                    case 'D':
                        msg.data = "D";
                        RCLCPP_INFO(this->get_logger(), "D key pressed");
                        should_publish = true;
                        break;
                    case 'q':
                    case 'Q':
                        RCLCPP_INFO(this->get_logger(), "Quitting...");
                        running_ = false;
                        rclcpp::shutdown();
                        return;
                    default:
                        // Ignore other keys - no publishing
                        break;
                }
                
                if (should_publish) {
                    publisher_->publish(msg);
                }
            }
            
            // Small sleep to prevent high CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
    std::thread keyboard_thread_;
    std::atomic<bool> running_;
    struct termios old_tio_, new_tio_;
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<KeyboardPublisher>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
