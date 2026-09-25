#include "mhs/ros_client.h"

#include <random>
#include <thread>

#include "google/protobuf/util/json_util.h"
#include "ros2/utils/qos_setting.h"

namespace mhs {
namespace {
using google::protobuf::Struct;
Struct Error(const std::string& message) {
  Struct out;
  (*out.mutable_fields())["error"].set_string_value(message);
  return out;
}
}  // namespace

RosClient::RosClient(const ros2_actuator::Device& device)
    : device_id_(device.actuator.actuator_name()) {
  std::random_device random;
  prefix_ = std::to_string(random()) + "_" + std::to_string(random());
  node_ = std::make_shared<rclcpp::Node>("mhs_bridge_" + prefix_);
  executor_.add_node(node_);
  const auto qos = ros2_utils::CreateQosSetting(device.node.qos_setting());
  publisher_ = node_->create_publisher<std_msgs::msg::String>(device.command_topic, qos);
  subscription_ = node_->create_subscription<std_msgs::msg::String>(
      device.status_topic, qos, [this](std_msgs::msg::String::ConstSharedPtr msg) {
        Struct result;
        if (msg->data.size() > 65536 ||
            !google::protobuf::util::JsonStringToMessage(msg->data, &result).ok())
          return;
        const auto id = result.fields().find("request_id");
        if (id == result.fields().end() || id->second.string_value() != pending_id_) return;
        response_ = std::move(result);
        received_ = true;
      });
}

Struct RosClient::Exchange(Struct request) {
  pending_id_ = prefix_ + "-" + std::to_string(++sequence_);
  received_ = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  // Wait for both directions of discovery before sending; never replay a write.
  while (rclcpp::ok() &&
         (publisher_->get_subscription_count() != 1 || subscription_->get_publisher_count() != 1)) {
    if (std::chrono::steady_clock::now() >= deadline)
      return Error("ROS actuator unavailable or multiple owners; command not sent");
    executor_.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  auto& fields = *request.mutable_fields();
  fields["request_id"].set_string_value(pending_id_);
  fields["session_id"].set_string_value(session_id_);
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  fields["expires_unix_ms"].set_number_value(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count() + 1000);
  std_msgs::msg::String message;
  if (!google::protobuf::util::MessageToJsonString(request, &message.data).ok())
    return Error("Cannot serialize ROS request");
  publisher_->publish(message);
  const auto response_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (rclcpp::ok() && std::chrono::steady_clock::now() < response_deadline) {
    executor_.spin_some();
    if (received_) {
      const auto session = response_.fields().find("session_id");
      if (session == response_.fields().end() || session->second.string_value().empty()) {
        uncertain_ = true;
        return Error("Missing actuator session identity");
      }
      if (session_id_.empty())
        session_id_ = session->second.string_value();
      else if (session_id_ != session->second.string_value()) {
        uncertain_ = true;
        return Error("Actuator session changed; reconnect after operator review");
      }
      return response_;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  uncertain_ = true;
  return Error("ROS acknowledgment timeout; command outcome uncertain, do not retry motion");
}

Struct RosClient::Request(Struct request) {
  const auto op = request.fields().find("operation");
  const bool write = op != request.fields().end() && op->second.string_value() == "write_position";
  if (write && uncertain_) return Error("Previous command uncertain; operator review required");
  if (session_id_.empty()) {
    Struct discovery;
    (*discovery.mutable_fields())["operation"].set_string_value("list_devices");
    auto result = Exchange(discovery);
    if (result.fields().contains("error")) return result;
  }
  auto result = Exchange(std::move(request));
  if (write && uncertain_) {
    // An ambiguous write ends this client session. The ROS node
    // independently monitors active moves even if this process disappears.
    auto stopped = Stop();
    (*result.mutable_fields())["stop_result"].mutable_struct_value()->CopyFrom(stopped);
  }
  return result;
}

Struct RosClient::Stop() {
  if (session_id_.empty()) return Error("No ROS session; stop unconfirmed");
  Struct request;
  (*request.mutable_fields())["operation"].set_string_value("stop_device");
  (*request.mutable_fields())["device_id"].set_string_value(device_id_);
  return Exchange(request);
}
}  // namespace mhs
