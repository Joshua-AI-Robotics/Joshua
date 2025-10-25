#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "config/proto/config.pb.h"
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <cmath>
#include "ros2/node_runner.h"

class LidarPublisher : public rclcpp::Node {
private:
  struct Lidar {
    std::string topic;
    std::unique_ptr<robot::perception::PerceptionInterface> interface;
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr publisher;
    rclcpp::TimerBase::SharedPtr timer;
  };

public:
LidarPublisher(const std::string& node_name, const int node_id, const config::Config& config) 
    : Node(node_name) {    
    for (const auto& single_perception : config.robot().perceptions().single_perceptions()) {
      if (single_perception.perception_type() == robot::perception::PerceptionType::LIDAR && 
          static_cast<int>(single_perception.node_id()) == node_id) {
        const auto& lidar_proto = single_perception.lidar();
        
        // First, create the interface and check if it's valid.
        auto interface = robot::perception::PerceptionFactory::CreatePerception(single_perception);
        if (!interface.ok()) {
            RCLCPP_ERROR(this->get_logger(), "Failed to create perception interface for lidar '%s'. Check hardware connection or permissions.", 
                         lidar_proto.lidar_name().c_str());
            continue; // Skip this lidar if initialization failed.
        }

        lidars_.emplace_back(Lidar{
          .topic = single_perception.publish_topic(),
          .interface = std::move(interface.value()),
          .publisher = this->create_publisher<std_msgs::msg::Float32>(single_perception.publish_topic(), 10),
          .timer = this->create_wall_timer(
            std::chrono::milliseconds(1000 / single_perception.publish_rate_hz()),
            std::bind(&LidarPublisher::publish_lidar_data, this))
        });

        RCLCPP_INFO(this->get_logger(), "Found lidar '%s' in configuration for node_id %d. Publishing on topic: %s", 
                   lidar_proto.lidar_name().c_str(), node_id, single_perception.publish_topic().c_str());
      }
    }
    
    if (lidars_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No lidars found in configuration for node_id %d!", node_id);
      return;
    }
      
    RCLCPP_INFO(this->get_logger(), "Encoder publisher node started with %zu lidars for node_id %d!", 
               lidars_.size(), node_id);
  }

private:
  void publish_lidar_data() {
    if (lidars_.empty()) {
      RCLCPP_WARN(this->get_logger(), "No lidars initialized, skipping publish cycle.");
      return;
    }
    
    try {
      for (auto& lidar : lidars_) {
        auto packet = lidar.interface->GetData();

        if (!packet.ok()) {
            RCLCPP_WARN(this->get_logger(), "Failed to get data from lidar '%s'!", lidar.topic.c_str());
            continue;
        }
        
        // TODO(hmoon): Need to convert to pointCloud.
        if (!packet.value().has_polar_coordinate()) {
            RCLCPP_WARN(this->get_logger(), "Failed to get position data from lidar '%s'!", lidar.topic.c_str());
            continue;
        }

        // Get the first polar coordinate point's distance
        const auto& polar_data = packet.value().polar_coordinate();
        if (polar_data.polar_coordinates_size() == 0) {
            RCLCPP_WARN(this->get_logger(), "No polar coordinates in lidar data from '%s'!", lidar.topic.c_str());
            continue;
        }

        // Print all angle and distance values
        RCLCPP_INFO(this->get_logger(), "LiDAR '%s' scan with %d points:", lidar.topic.c_str(), polar_data.polar_coordinates_size());
        
        for (int i = 0; i < polar_data.polar_coordinates_size(); i++) {
            const auto& point = polar_data.polar_coordinates(i);
            RCLCPP_INFO(this->get_logger(), "  Point %d: angle=%u, distance=%u, intensity=%u", 
                       i, point.angle(), point.distance(), point.intensity());
        }
        
        // Still publish the first point's distance for compatibility
        auto polar_coordinate_data = polar_data.polar_coordinates(0).distance();
        auto message = std_msgs::msg::Float32();
        message.data = polar_coordinate_data;
        lidar.publisher->publish(message);
      }
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error publishing lidar data: %s", e.what());
    }
  }
  
  
  std::vector<Lidar> lidars_;
};

int main(int argc, char* argv[]) {
  // For test run:
  // bazel run ros2:lidar_publisher -- test_lidar 1 config/config_preset/lds01_lidar.pbtxt
  return ros2_utils::RunNode<LidarPublisher>(argc, argv, "lidar_publisher");
} 