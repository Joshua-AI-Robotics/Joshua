#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "robot/perception/proto/perception.pb.h"
#include "ros2/node_runner.h"
#include "ros2/proto/ros2_data_type.pb.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"
#include "sensor_msgs/msg/image.hpp"

namespace {
constexpr auto kCameraRetryBackoff = std::chrono::seconds(1);

bool ValidateCameraPublisher(const ros2::data_type::Ros2DataType ros2_data_type) {
  return ros2_data_type == ros2::data_type::IMAGE;
}
}  // namespace

class CameraPublisher : public rclcpp::Node {
 public:
  using Clock = std::chrono::steady_clock;
  using Now = std::function<Clock::time_point()>;
  using Factory =
      std::function<absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>>(
          const robot::perception::SinglePerception&)>;

  CameraPublisher(const std::string& node_name, int node_id, const config::Config& config);
  // Inject the source and retry clock for hardware-free lifecycle tests.
  CameraPublisher(const std::string& node_name,
                  int node_id,
                  const config::Config& config,
                  Factory factory,
                  Now now);

 private:
  // Initialization and recovery belong to the physical source, shared by all
  // its publishers. An unavailable source remains registered for later retries.
  struct Source {
    std::string name;
    std::unique_ptr<robot::perception::PerceptionInterface> interface;
    bool initialized = false;
    Clock::time_point retry_after{};
  };
  struct Camera {
    std::string topic;
    std::shared_ptr<Source> source;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher;
    rclcpp::TimerBase::SharedPtr timer;
  };

  bool InitializeCamera(Source& source);
  void ResetCamera(Source& source);
  void PublishCameraData();

  Now now_;
  std::vector<Camera> cameras_;
};

CameraPublisher::CameraPublisher(const std::string& node_name,
                                 int node_id,
                                 const config::Config& config)
    : CameraPublisher(
          node_name,
          node_id,
          config,
          [&config](const auto& sensor) {
            return robot::perception::PerceptionFactory::CreatePerception(sensor,
                                                                          config.robot().boards());
          },
          Clock::now) {}

CameraPublisher::CameraPublisher(const std::string& node_name,
                                 int node_id,
                                 const config::Config& config,
                                 Factory factory,
                                 Now now)
    : Node(node_name), now_(std::move(now)) {
  for (const auto& single_perception : config.robot().perceptions().single_perceptions()) {
    if (single_perception.sensor_type() != robot::perception::SensorType::IMAGE ||
        static_cast<int>(single_perception.node().id()) != node_id) {
      continue;
    }

    const std::string sensor_name = single_perception.sensor_name();
    const auto& qos_setting = single_perception.node().qos_setting();
    auto interface = factory(single_perception);
    if (!interface.ok()) {
      RCLCPP_ERROR(this->get_logger(),
                   "Failed to create perception interface for camera '%s': %s",
                   sensor_name.c_str(),
                   std::string(interface.status().message()).c_str());
      continue;
    }

    auto source = std::make_shared<Source>();
    source->name = sensor_name;
    source->interface = std::move(interface.value());
    // Keep the source and its endpoints even if the first open attempt fails.
    InitializeCamera(*source);

    for (const auto& publisher : single_perception.node().publishers()) {
      if (!ValidateCameraPublisher(publisher.ros2_data_type())) {
        RCLCPP_ERROR(this->get_logger(),
                     "Invalid publisher config for camera topic '%s' "
                     "(ros2_data_type=%d). Require IMAGE.",
                     publisher.topic().c_str(),
                     static_cast<int>(publisher.ros2_data_type()));
        continue;
      }

      const auto qos = ros2_utils::CreateQosSetting(qos_setting);
      cameras_.emplace_back(Camera{
          .topic = publisher.topic(),
          .source = source,
          .publisher = this->create_publisher<sensor_msgs::msg::Image>(publisher.topic(), qos),
          .timer =
              this->create_wall_timer(std::chrono::milliseconds(1000 / publisher.publish_rate_hz()),
                                      [this]() { PublishCameraData(); })});
    }

    RCLCPP_INFO(this->get_logger(),
                "Found camera '%s' in configuration for node_id %d. Publishing on %zu topics",
                sensor_name.c_str(),
                node_id,
                static_cast<size_t>(single_perception.node().publishers().size()));
  }

  if (cameras_.empty()) {
    RCLCPP_ERROR(this->get_logger(), "No camera found in configuration for node_id %d!", node_id);
    return;
  }

  RCLCPP_INFO(this->get_logger(),
              "Camera publisher node started with %zu camera topics for node_id %d!",
              cameras_.size(),
              node_id);
}

bool CameraPublisher::InitializeCamera(Source& source) {
  const auto status = source.interface->Init();
  if (status.ok()) {
    source.initialized = true;
    return true;
  }

  ResetCamera(source);
  if (absl::IsInvalidArgument(status)) {
    throw std::invalid_argument("Invalid camera configuration for '" + source.name +
                                "': " + status.ToString());
  }
  RCLCPP_WARN(this->get_logger(),
              "Camera '%s' unavailable; retrying initialization in one second: %s",
              source.name.c_str(),
              status.ToString().c_str());
  return false;
}

void CameraPublisher::ResetCamera(Source& source) {
  source.initialized = false;
  const auto status = source.interface->Teardown();
  if (!status.ok()) {
    RCLCPP_WARN(this->get_logger(),
                "Failed to close camera '%s': %s",
                source.name.c_str(),
                status.ToString().c_str());
  }
  source.retry_after = now_() + kCameraRetryBackoff;
}

void CameraPublisher::PublishCameraData() {
  if (cameras_.empty()) {
    RCLCPP_ERROR(this->get_logger(), "Camera not initialized!");
    return;
  }

  try {
    for (const auto& camera : cameras_) {
      auto& source = *camera.source;
      if (!source.initialized) {
        if (now_() < source.retry_after || !InitializeCamera(source)) continue;
      }
      auto packet = source.interface->GetData();

      if (!packet.ok()) {
        ResetCamera(source);
        RCLCPP_WARN(this->get_logger(),
                    "Failed to get data from camera '%s'; retrying in one second: %s",
                    source.name.c_str(),
                    packet.status().ToString().c_str());
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

      camera.publisher->publish(*image_msg);
    }
  } catch (const std::exception& e) {
    RCLCPP_ERROR(this->get_logger(), "Error publishing camera data: %s", e.what());
  }
}

#ifndef JOSHUA_CAMERA_PUBLISHER_TEST
int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<CameraPublisher>(argc, argv, "camera_publisher");
}
#endif
