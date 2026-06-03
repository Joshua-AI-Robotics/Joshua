#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "ros2/node_runner.h"
#include "ros2/proto/node.pb.h"
#include "ros2/proto/ros2_data_type.pb.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"

namespace {

ros2::node::PayloadType EffectivePayloadType(const ros2::node::PayloadType payload_type) {
  if (payload_type == ros2::node::PAYLOAD_TYPE_UNSPECIFIED) {
    return ros2::node::PAYLOAD_TYPE_PERCEPTION_PACKET;
  }
  return payload_type;
}

bool ValidatePerceptionPublisher(const ros2::data_type::Ros2DataType ros2_data_type,
                                 const ros2::node::PayloadType payload_type) {
  const auto resolved = EffectivePayloadType(payload_type);
  if (resolved == ros2::node::PAYLOAD_TYPE_ACTION_PACKET) {
    return false;
  }
  if (ros2_data_type == ros2::data_type::UINT8_MULTI_ARRAY) {
    return resolved == ros2::node::PAYLOAD_TYPE_PERCEPTION_PACKET;
  }
  if (ros2_data_type == ros2::data_type::IMAGE) {
    return true;
  }
  return false;
}

}  // namespace

class CameraPublisher : public rclcpp::Node {
 private:
  struct Camera {
    std::string topic;
    std::shared_ptr<robot::perception::PerceptionInterface> interface;
    rclcpp::PublisherBase::SharedPtr publisher;
    rclcpp::TimerBase::SharedPtr timer;
    ros2::data_type::Ros2DataType ros2_data_type;
    ros2::node::PayloadType payload_type;
  };

 public:
  CameraPublisher(const std::string& node_name, const int node_id, const config::Config& config)
      : Node(node_name) {
    for (const auto& single_perception : config.robot().perceptions().single_perceptions()) {
      if (single_perception.perception_type() != robot::perception::PerceptionType::CAMERA ||
          static_cast<int>(single_perception.node().id()) != node_id) {
        continue;
      }

      const auto& camera_proto = single_perception.camera();
      const auto& qos_setting = single_perception.node().qos_setting();

      auto interface = robot::perception::PerceptionFactory::CreatePerception(single_perception);
      if (!interface.ok()) {
        RCLCPP_ERROR(this->get_logger(),
                     "Failed to create perception interface for camera '%s'. Check hardware "
                     "connection or permissions.",
                     camera_proto.camera_name().c_str());
        continue;
      }

      auto shared_interface =
          std::shared_ptr<robot::perception::PerceptionInterface>(std::move(interface.value()));

      for (const auto& publisher : single_perception.node().publishers()) {
        if (!ValidatePerceptionPublisher(publisher.ros2_data_type(), publisher.payload_type())) {
          RCLCPP_ERROR(this->get_logger(),
                       "Invalid publisher config for camera topic '%s' "
                       "(ros2_data_type=%d, payload_type=%d).",
                       publisher.topic().c_str(),
                       static_cast<int>(publisher.ros2_data_type()),
                       static_cast<int>(publisher.payload_type()));
          continue;
        }

        Camera camera_entry{.topic = publisher.topic(),
                            .interface = shared_interface,
                            .ros2_data_type = publisher.ros2_data_type(),
                            .payload_type = publisher.payload_type()};

        const auto qos = ros2_utils::CreateQosSetting(qos_setting);
        if (publisher.ros2_data_type() == ros2::data_type::UINT8_MULTI_ARRAY) {
          camera_entry.publisher =
              this->create_publisher<std_msgs::msg::UInt8MultiArray>(publisher.topic(), qos);
        } else {
          camera_entry.publisher =
              this->create_publisher<sensor_msgs::msg::Image>(publisher.topic(), qos);
        }

        camera_entry.timer =
            this->create_wall_timer(std::chrono::milliseconds(1000 / publisher.publish_rate_hz()),
                                    [this]() { publish_camera_data(); });

        cameras_.emplace_back(std::move(camera_entry));
      }

      RCLCPP_INFO(this->get_logger(),
                  "Found camera '%s' in configuration for node_id %d. Publishing on %zu topics",
                  camera_proto.camera_name().c_str(),
                  node_id,
                  single_perception.node().publishers().size());
    }

    if (cameras_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No camera found in configuration for node_id %d!", node_id);
      return;
    }

    RCLCPP_INFO(this->get_logger(),
                "Camera publisher node started with %zu cameras for node_id %d!",
                cameras_.size(),
                node_id);
  }

 private:
  void publish_camera_data() {
    if (cameras_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "Camera not initialized!");
      return;
    }

    try {
      for (const auto& camera : cameras_) {
        auto packet = camera.interface->GetData();

        if (!packet.ok()) {
          RCLCPP_WARN(
              this->get_logger(), "Failed to get data from camera '%s'!", camera.topic.c_str());
          continue;
        }

        if (camera.ros2_data_type == ros2::data_type::UINT8_MULTI_ARRAY) {
          const auto image_status = ros2_utils::RequirePerceptionImage(packet.value());
          if (!image_status.ok()) {
            RCLCPP_WARN(this->get_logger(),
                        "Failed to get image data from camera '%s': %s",
                        camera.topic.c_str(),
                        image_status.message().data());
            continue;
          }

          const auto serialized = ros2_utils::SerializePerceptionPacket(packet.value());
          auto message = std_msgs::msg::UInt8MultiArray();
          message.data.assign(serialized.begin(), serialized.end());

          auto pub = std::dynamic_pointer_cast<rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>>(
              camera.publisher);
          if (pub) {
            pub->publish(message);
          }
          continue;
        }

        const auto image_status = ros2_utils::RequirePerceptionImage(packet.value());
        if (!image_status.ok()) {
          RCLCPP_WARN(this->get_logger(),
                      "Failed to get image data from camera '%s': %s",
                      camera.topic.c_str(),
                      image_status.message().data());
          continue;
        }

        const auto& image_data = packet.value().image();

        if (image_data.width() <= 0 || image_data.height() <= 0 || image_data.channels() <= 0) {
          RCLCPP_ERROR(this->get_logger(),
                       "Invalid image dimensions: %dx%d, channels=%d",
                       image_data.width(),
                       image_data.height(),
                       image_data.channels());
          continue;
        }

        auto image_msg = std::make_unique<sensor_msgs::msg::Image>();
        image_msg->header.stamp = this->now();
        image_msg->header.frame_id = "camera_frame";
        image_msg->height = image_data.height();
        image_msg->width = image_data.width();
        image_msg->encoding = "rgb8";
        image_msg->is_bigendian = false;
        image_msg->step = image_data.width() * 3;

        size_t data_size = image_data.width() * image_data.height() * 3;
        image_msg->data.resize(data_size);

        const uint8_t* bgr_data = reinterpret_cast<const uint8_t*>(image_data.data().data());
        for (size_t i = 0; i < data_size; i += 3) {
          image_msg->data[i] = bgr_data[i + 2];
          image_msg->data[i + 1] = bgr_data[i + 1];
          image_msg->data[i + 2] = bgr_data[i];
        }

        auto pub =
            std::dynamic_pointer_cast<rclcpp::Publisher<sensor_msgs::msg::Image>>(camera.publisher);
        if (pub) {
          pub->publish(*image_msg);
        }
      }
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error publishing camera data: %s", e.what());
    }
  }

  std::vector<Camera> cameras_;
};

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<CameraPublisher>(argc, argv, "camera_publisher");
}
