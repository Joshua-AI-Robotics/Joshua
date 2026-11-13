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
#include "ros2/logging.h"
#include "ros2/node_runner.h"
#include "ros2/proto/ros2_data_type.pb.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"
#include "ros2/utils/robot_time_conversion.h"
#include "ros2/utils/robot_timer.h"
#include "sensor_msgs/msg/image.hpp"
#include "utils/robot_time.h"

namespace {
constexpr double kCameraRetryBackoffSeconds = 1.0;

bool ValidateCameraPublisher(const ros2::data_type::Ros2DataType ros2_data_type) {
  return ros2_data_type == ros2::data_type::IMAGE;
}
}  // namespace

class CameraPublisher : public rclcpp::Node {
 public:
  // Retry times are seconds in the configured robot clock's domain.
  using Now = std::function<double()>;
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
    double retry_after = 0.0;
  };
  struct Camera {
    std::string topic;
    std::shared_ptr<Source> source;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher;
    rclcpp::TimerBase::SharedPtr timer;
  };

  bool InitializeCamera(Source& source);
  void ResetCamera(Source& source);
  void PublishCameraData(size_t camera_index);

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
          joshua::RobotTime) {}

CameraPublisher::CameraPublisher(const std::string& node_name,
                                 int node_id,
                                 const config::Config& config,
                                 Factory factory,
                                 Now now)
    : Node(node_name), now_(std::move(now)) {
  ros2_utils::SetLogNodeName(get_logger().get_name());
  for (const auto& single_perception : config.robot().perceptions().single_perceptions()) {
    if (single_perception.sensor_type() != robot::perception::SensorType::IMAGE ||
        static_cast<int>(single_perception.node().id()) != node_id) {
      continue;
    }

    const std::string sensor_name = single_perception.sensor_name();
    const auto& qos_setting = single_perception.node().qos_setting();
    auto interface = factory(single_perception);
    if (!interface.ok()) {
      JOSHUA_LOG(ERROR) << "Failed to create perception interface for camera '" << sensor_name
                        << "': " << interface.status();
      continue;
    }

    auto source = std::make_shared<Source>();
    source->name = sensor_name;
    source->interface = std::move(interface.value());
    // Keep the source and its endpoints even if the first open attempt fails.
    InitializeCamera(*source);

    for (const auto& publisher : single_perception.node().publishers()) {
      if (!ValidateCameraPublisher(publisher.ros2_data_type())) {
        JOSHUA_LOG(ERROR) << "Invalid publisher config for camera topic '" << publisher.topic()
                          << "' (ros2_data_type=" << static_cast<int>(publisher.ros2_data_type())
                          << "). Require IMAGE.";
        continue;
      }

      const auto qos = ros2_utils::CreateQosSetting(qos_setting);
      const size_t camera_index = cameras_.size();
      cameras_.emplace_back(Camera{
          .topic = publisher.topic(),
          .source = source,
          .publisher = this->create_publisher<sensor_msgs::msg::Image>(publisher.topic(), qos),
          .timer = ros2_utils::CreateRobotTimer(
              *this, 1.0 / publisher.publish_rate_hz(), [this, camera_index]() {
                PublishCameraData(camera_index);
              })});
    }

    JOSHUA_LOG(INFO) << "Found camera '" << sensor_name << "' in configuration for node_id "
                     << node_id << ". Publishing on "
                     << single_perception.node().publishers().size() << " topics";
  }

  if (cameras_.empty()) {
    JOSHUA_LOG(ERROR) << "No camera found in configuration for node_id " << node_id << "!";
    return;
  }

  JOSHUA_LOG(INFO) << "Camera publisher node started with " << cameras_.size()
                   << " camera topics for node_id " << node_id << "!";
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
  JOSHUA_LOG(WARNING) << "Camera '" << source.name
                      << "' unavailable; retrying initialization in one second: " << status;
  return false;
}

void CameraPublisher::ResetCamera(Source& source) {
  source.initialized = false;
  const auto status = source.interface->Teardown();
  if (!status.ok()) {
    JOSHUA_LOG(WARNING) << "Failed to close camera '" << source.name << "': " << status;
  }
  source.retry_after = now_() + kCameraRetryBackoffSeconds;
}

void CameraPublisher::PublishCameraData(size_t camera_index) {
  const auto& camera = cameras_.at(camera_index);
  auto& source = *camera.source;
  if (!source.initialized) {
    if (now_() < source.retry_after || !InitializeCamera(source)) return;
  }
  auto packet = source.interface->GetData();

  if (!packet.ok()) {
    ResetCamera(source);
    JOSHUA_LOG(WARNING) << "Failed to get data from camera '" << source.name
                        << "'; retrying in one second: " << packet.status();
    return;
  }

  const auto image_status = ros2_utils::RequirePerceptionImage(packet.value());
  if (!image_status.ok()) {
    JOSHUA_LOG(WARNING) << "Failed to get image data from camera '" << camera.topic
                        << "': " << image_status;
    return;
  }

  const auto& image_data = packet.value().image();
  if (image_data.width() <= 0 || image_data.height() <= 0 || image_data.channels() <= 0) {
    JOSHUA_LOG(ERROR) << "Invalid image dimensions: " << image_data.width() << "x"
                      << image_data.height() << ", channels=" << image_data.channels();
    return;
  }

  auto image_msg = std::make_unique<sensor_msgs::msg::Image>();
  const auto stamp = ros2_utils::ToRosTimestamp(packet->timestamp());
  if (!stamp.ok()) {
    JOSHUA_LOG(ERROR) << "Invalid camera timestamp: " << stamp.status();
    return;
  }
  image_msg->header.stamp = *stamp;
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

#ifndef JOSHUA_CAMERA_PUBLISHER_TEST
JOSHUA_MAIN(argc, argv) {
  return ros2_utils::RunNode<CameraPublisher>(argc, argv, "camera_publisher");
}
#endif
