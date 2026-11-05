#include "config/robot_capabilities.h"

#include <cmath>
#include <map>
#include <set>
#include <string>

#include "robot/board/factory/board_resolver.h"
#include "ros2/utils/packet_parser.h"
#include "version.h"

namespace config {
namespace {
using google::protobuf::Value;
Value Object() {
  Value v;
  v.mutable_struct_value();
  return v;
}
Value Array() {
  Value v;
  v.mutable_list_value();
  return v;
}
Value Text(const std::string& s) {
  Value v;
  v.set_string_value(s);
  return v;
}
Value Number(double n) {
  Value v;
  v.set_number_value(n);
  return v;
}
void Set(Value& v, const std::string& k, Value x) {
  (*v.mutable_struct_value()->mutable_fields())[k] = std::move(x);
}
void Add(Value& v, Value x) {
  *v.mutable_list_value()->add_values() = std::move(x);
}
void Gap(Value& gaps, const std::string& target, const std::string& reason) {
  Value gap = Object();
  Set(gap, "target", Text(target));
  Set(gap, "reason", Text(reason));
  Add(gaps, std::move(gap));
}
bool FeetechPath(const Config& cfg, const std::string& name, uint32_t index) {
  auto resolved = robot::board::ResolveChannelConfig(
      cfg.robot().boards(), "capability inspection", name, index);
  if (!resolved.ok()) return false;
  const auto& b = *resolved->board;
  // Bound this descriptor to addressable, distinct vendor channels. This is
  // evidence selection, not a replacement for board initialization checks.
  std::set<uint32_t> wire_ids;
  for (const auto& channel : b.channels()) {
    if (channel.drive() != robot::board::SERVO_BUS_UART || !channel.has_servo_bus() ||
        channel.servo_bus().servo_id() == 0 || channel.servo_bus().servo_id() > 255 ||
        !wire_ids.insert(channel.servo_bus().servo_id()).second)
      return false;
  }
  return b.board_type() == robot::board::FEETECH_BUS &&
         b.protocol() == robot::board::BOARD_PROTOCOL_UNSPECIFIED &&
         resolved->channel->drive() == robot::board::SERVO_BUS_UART &&
         resolved->channel->has_servo_bus() && b.comm().comm_type() == robot::comm::SERIAL &&
         b.comm().transport_type() == robot::comm::MESSAGE && b.comm().has_serial_config() &&
         b.comm().serial_config().message_adapter() == robot::comm::CALLER_SIZED;
}
Value Target(const std::string& id, const std::string& board, uint32_t channel) {
  Value v = Object();
  Set(v, "device_id", Text(id));
  Set(v, "board_name", Text(board));
  Set(v, "channel", Number(channel));
  Set(v, "units", Text("native_ticks"));
  Set(v, "reference", Text("device_zero; not a calibrated world/tool frame"));
  return v;
}
Value Endpoint(const std::string& topic,
               ros2::data_type::Ros2DataType type,
               const std::string& command) {
  Value v = Object();
  Set(v, "topic", Text(topic));
  Set(v, "message_type", Text(ros2::data_type::Ros2DataType_Name(type)));
  Set(v, "command", Text(command));
  Set(v, "units", Text(type == ros2::data_type::JOINT_STATE ? "radians" : "native_ticks"));
  return v;
}
Value Operation(const std::string& name,
                const std::string& parameters,
                const std::string& contract,
                const std::string& verification,
                Value targets) {
  Value v = Object();
  Set(v, "name", Text(name));
  Set(v, "parameters", Text(parameters));
  Set(v, "driver_contract", Text(contract));
  Set(v, "verification", Text(verification));
  Set(v, "mcp_execution", Text("unavailable"));
  Set(v, "targets", std::move(targets));
  return v;
}
}  // namespace
Value BuildRobotCapabilities(const Config& cfg) {
  Value data = Object(), positions = Array(), observations = Array(), gaps = Array();
  std::map<std::string, size_t> actuator_names, sensor_names;
  for (const auto& action : cfg.robot().actions().single_actions())
    ++actuator_names[action.actuator().actuator_name()];
  for (const auto& sensor : cfg.robot().perceptions().single_perceptions())
    ++sensor_names[sensor.sensor_name()];
  for (const auto& [name, count] : actuator_names) {
    if (count > 1)
      Gap(gaps,
          "actuator:" + name,
          "Configured actuator identity is ambiguous; duplicate names cannot identify an "
          "independently addressable target.");
  }
  for (const auto& [name, count] : sensor_names) {
    if (count > 1)
      Gap(gaps,
          "sensor:" + name,
          "Configured sensor identity is ambiguous; duplicate names cannot identify an "
          "independently addressable observation or feedback association.");
  }
  for (const auto& action : cfg.robot().actions().single_actions()) {
    const auto& a = action.actuator();
    const std::string id = "actuator:" + a.actuator_name();
    if (actuator_names.at(a.actuator_name()) != 1) continue;
    if (action.action_type() != robot::action::ACTUATOR ||
        a.motor_type() != robot::action::MOTOR_STS3215 || !a.has_sts3215_config() ||
        !FeetechPath(cfg, a.board_name(), a.channel())) {
      Gap(gaps,
          id,
          "No capability evidence for this configured motor/board/channel/transport combination.");
      continue;
    }
    if (!std::isfinite(a.operational_lower_limit()) ||
        !std::isfinite(a.operational_upper_limit()) || !std::isfinite(a.physical_lower_limit()) ||
        !std::isfinite(a.physical_upper_limit()) ||
        !std::isfinite(a.sts3215_config().idle_position()) ||
        a.operational_lower_limit() > a.operational_upper_limit() ||
        a.physical_lower_limit() > a.physical_upper_limit()) {
      Gap(gaps,
          id,
          "Position contract has nonfinite or reversed descriptive bounds/idle values; no numeric "
          "capability is evidenced.");
      continue;
    }
    if (a.operational_lower_limit() < 0 || a.operational_upper_limit() > 65535) {
      Gap(gaps,
          id,
          "Operational position bounds exceed the FEETECH_BUS uint16 wire range [0, 65535]; "
          "the board clamps these values, so the configured numeric contract is not evidenced.");
      continue;
    }
    Value target = Target(id, a.board_name(), a.channel()), endpoints = Array(), feedback = Array();
    Set(target, "operational_lower_limit", Number(a.operational_lower_limit()));
    Set(target, "operational_upper_limit", Number(a.operational_upper_limit()));
    Set(target, "physical_lower_limit", Number(a.physical_lower_limit()));
    Set(target, "physical_upper_limit", Number(a.physical_upper_limit()));
    Set(target, "idle_position", Number(a.sts3215_config().idle_position()));
    if (action.node().node_type() == ros2::node::ACTUATOR_SUBSCRIBER) {
      for (const auto& sub : action.node().subscriptions()) {
        if (!ros2_utils::ValidateActionMessageType(sub, a).ok()) continue;
        auto command = ros2_utils::ParseActionTypeFromTopic(sub.topic());
        if (sub.ros2_data_type() == ros2::data_type::FLOAT32 && command.ok() &&
            *command == "position")
          Add(endpoints, Endpoint(sub.topic(), sub.ros2_data_type(), "position"));
        else
          Gap(gaps,
              id,
              "No position-exposure evidence for endpoint " + sub.topic() +
                  "; initial report covers FLOAT32 native position only, not SI, integer, velocity "
                  "or preset endpoints.");
      }
    }
    bool ambiguous_feedback = false;
    for (const auto& s : cfg.robot().perceptions().single_perceptions()) {
      if (s.sensor_type() == robot::perception::POSITION && s.has_sts3215_encoder_config() &&
          s.sts3215_encoder_config().board_name() == a.board_name() &&
          s.sts3215_encoder_config().channel() == a.channel()) {
        if (sensor_names.at(s.sensor_name()) != 1)
          ambiguous_feedback = true;
        else
          Add(feedback, Text("sensor:" + s.sensor_name()));
      }
    }
    if (a.sts3215_config().idle_position() < a.operational_lower_limit() ||
        a.sts3215_config().idle_position() > a.operational_upper_limit())
      Gap(gaps,
          id,
          "Configured idle_position is outside operational position bounds; idle and teardown "
          "deliberately bypass the ordinary position check.");
    if (feedback.list_value().values().empty())
      Gap(gaps,
          id,
          ambiguous_feedback
              ? "Configured POSITION feedback has an ambiguous sensor identity; no unique "
                "feedback association is evidenced."
              : "No configured POSITION sensor on this actuator's board/channel; matching topics "
                "do not establish feedback association.");
    if (endpoints.list_value().values().empty())
      Gap(gaps, id, "No evidenced position command endpoint on ACTUATOR_SUBSCRIBER.");
    Set(target,
        "ros_exposure",
        Text(endpoints.list_value().values().empty() ? "not_evidenced" : "configured"));
    Set(target, "configured_ros", std::move(endpoints));
    Set(target, "feedback_sensor_ids", std::move(feedback));
    Add(positions, std::move(target));
  }
  for (const auto& s : cfg.robot().perceptions().single_perceptions()) {
    const std::string id = "sensor:" + s.sensor_name();
    if (sensor_names.at(s.sensor_name()) != 1) continue;
    if (s.sensor_type() != robot::perception::POSITION || !s.has_sts3215_encoder_config() ||
        !FeetechPath(
            cfg, s.sts3215_encoder_config().board_name(), s.sts3215_encoder_config().channel())) {
      Gap(gaps, id, "No position observation capability evidence for this configured sensor path.");
      continue;
    }
    Value target = Target(
              id, s.sts3215_encoder_config().board_name(), s.sts3215_encoder_config().channel()),
          endpoints = Array();
    bool joint_state_candidate = false;
    if (s.node().node_type() == ros2::node::POSITION_PUBLISHER) {
      for (const auto& pub : s.node().publishers()) {
        if (ros2_utils::ValidatePositionMessageType(pub.ros2_data_type(), s).ok()) {
          Add(endpoints, Endpoint(pub.topic(), pub.ros2_data_type(), "measured_position"));
          if (pub.ros2_data_type() == ros2::data_type::JOINT_STATE) joint_state_candidate = true;
        }
      }
    }
    Set(target, "configured_ros", std::move(endpoints));
    Set(target,
        "mcp_feedback",
        Text(joint_state_candidate
                 ? "requires_operator_preflight_selection_and_live_publisher_verification"
                 : "not_configured_for_get_feedback"));
    Add(observations, std::move(target));
  }
  Value operations = Array();
  Add(operations,
      Operation("joint_position_target",
                "device_id and native_ticks position within configured operational bounds; only "
                "targets with configured ROS exposure have an evidenced endpoint",
                "STS3215 accepts native position and SI position (radians), native nonnegative "
                "speed, and torque-enable/idle/middle/teardown presets; effort and SI velocity are "
                "unsupported. Only listed ROS position endpoints are described as exposed; driver "
                "acceptance does not create endpoints.",
                "No implemented target/task verifier. Verifying a commanded position would require "
                "fresh controlled-joint feedback, a known device-zero reference and an explicit "
                "tolerance; leader readings do not satisfy this requirement.",
                std::move(positions)));
  Add(operations,
      Operation("joint_position_observation",
                "sensor device_id; measured position in endpoint units",
                "STS3215 encoder reads its configured board/channel. get_feedback requires "
                "operator-selected POSITION_PUBLISHER with one exclusive JOINT_STATE topic and "
                "verified live publisher; config inspection supplies no live sample.",
                "A future verifier must compare fresh measured position from the controlled "
                "board/channel with the requested target in matching units/reference and explicit "
                "tolerance. Publisher identity and receipt freshness alone do not prove "
                "acquisition freshness, follower state or task completion.",
                std::move(observations)));
  Gap(gaps,
      "mcp_motion",
      "No MCP motion or task execution tool exists; this operation only inspects configuration.");
  Gap(gaps,
      "cartesian_and_pick_and_place",
      "No established Cartesian planner, calibrated world/tool frame, or pick-and-place "
      "implementation is evidenced.");
  Gap(gaps,
      "verified_cyclic_task",
      "No verified cyclic task executor or task-completion evidence is established.");
  Gap(gaps,
      "position_limits",
      "Operational bounds constrain ordinary position commands, not every preset. STS3215 "
      "SetIdlePosition and teardown deliberately bypass the operational check; idle_position may "
      "lie outside these bounds.");
  Set(data, "config_name", Text(cfg.general().name()));
  Set(data, "build_version", Text(JOSHUA_VERSION));
  Set(data, "hardware_readiness", Text("unknown"));
  Set(data, "operations", std::move(operations));
  Set(data, "gaps", std::move(gaps));
  Value prerequisites = Array();
  for (const auto* s :
       {"Operator authorization and qualified hardware setup, wiring, calibration and limits are "
        "required before motion.",
        "Configured devices, firmware, bus ownership, torque enable and live feedback have not "
        "been verified by this inspection.",
        "Software evidence covers the named driver/board/transport path, not every option or "
        "complete runtime compatibility. Declared ROS endpoints are not a live graph.",
        "get_feedback additionally requires runtime observation preflight, supported operation "
        "mode, bounded identities and one exclusive JOINT_STATE publisher; candidate status does "
        "not establish availability.",
        "Device zero is not a calibrated world/tool reference. Position readings do not establish "
        "task completion or actuator readiness."})
    Add(prerequisites, Text(s));
  Set(data, "prerequisites", std::move(prerequisites));
  Value evidence = Array();
  for (const auto* s : {"robot/action/motors/drivers/sts3215_driver.cc",
                        "robot/action/motors/drivers/sts3215_driver_test.cc",
                        "robot/board/feetech_bus/feetech_bus_board.cc",
                        "robot/board/feetech_bus/feetech_bus_board_test.cc",
                        "robot/perception/position/sts3215_position_sensor.cc",
                        "robot/perception/position/sts3215_position_sensor_test.cc",
                        "ros2/utils/packet_parser.cc",
                        "ros2/utils/packet_parser_cc_test.cc",
                        "node_generator/runtime_observation.cc",
                        "config/robot_capabilities_test.cc"})
    Add(evidence, Text(s));
  Set(data, "evidence", std::move(evidence));
  return data;
}
}  // namespace config
