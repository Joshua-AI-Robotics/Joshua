#include "ros2/actuator_command_endpoint.h"

#include <cmath>
#include <random>
#include <stdexcept>

#include "google/protobuf/util/json_util.h"
#include "ros2/utils/qos_setting.h"

namespace ros2_actuator {
namespace {
using google::protobuf::Struct;
std::string String(const Struct& s, const std::string& key) {
  const auto it = s.fields().find(key);
  return it == s.fields().end() ? "" : it->second.string_value();
}
Struct Error(const std::string& message) {
  Struct out;
  (*out.mutable_fields())["error"].set_string_value(message);
  return out;
}
}  // namespace

CommandEndpoint::CommandEndpoint(rclcpp::Node& node,
                                 Device device,
                                 std::shared_ptr<robot::action::ActionInterface> action)
    : session_(device) {
  const auto attached = session_.Attach(std::move(action));
  if (!attached.ok()) throw std::runtime_error(attached.ToString());
  std::random_device random;
  session_id_ = std::to_string(random()) + "-" + std::to_string(random());
  const auto qos = ros2_utils::CreateQosSetting(device.node.qos_setting());
  publisher_ = node.create_publisher<std_msgs::msg::String>(device.status_topic, qos);
  subscription_ = node.create_subscription<std_msgs::msg::String>(
      device.command_topic, qos, [this](std_msgs::msg::String::ConstSharedPtr msg) {
        Struct request;
        if (msg->data.size() > 65536 ||
            !google::protobuf::util::JsonStringToMessage(msg->data, &request).ok())
          return;
        auto response = Process(request);
        (*response.mutable_fields())["request_id"].set_string_value(String(request, "request_id"));
        (*response.mutable_fields())["session_id"].set_string_value(session_id_);
        std_msgs::msg::String result;
        if (google::protobuf::util::MessageToJsonString(response, &result.data).ok())
          publisher_->publish(result);
      });
  monitor_ = node.create_wall_timer(std::chrono::milliseconds(20), [this] { session_.Poll(); });
}

Struct CommandEndpoint::Process(const Struct& request) {
  const auto id = String(request, "request_id");
  if (id.empty() || id.size() > 128) return Error("A bounded request_id is required");
  const auto op = String(request, "operation");
  if (op == "list_devices" || op == "describe_device") return session_.Handle(request);
  if (String(request, "session_id") != session_id_)
    return Error("Actuator session changed; operator review required");
  // Never execute a command twice, including after completion or a lost reply.
  if (auto it = responses_.find(id); it != responses_.end()) return it->second;
  auto expiry = request.fields().find("expires_unix_ms");
  const double now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  if (expiry == request.fields().end() ||
      expiry->second.kind_case() != google::protobuf::Value::kNumberValue ||
      !std::isfinite(expiry->second.number_value()) || expiry->second.number_value() <= now ||
      expiry->second.number_value() > now + 5000)
    return Error("Expired or invalid command deadline; command not executed");
  // Reads and stops remain available after the bounded replay cache fills.
  if (op == "write_position" && responses_.size() >= 4096)
    return Error("Session command capacity reached; operator restart required");
  auto result = session_.Handle(request);
  if (op == "write_position") responses_.emplace(id, result);
  return result;
}
}  // namespace ros2_actuator
