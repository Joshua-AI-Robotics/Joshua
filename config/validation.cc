#include "config/validation.h"

#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "robot/board/factory/board_resolver.h"
#include "robot/comm/factory/comm_factory.h"
#include "ros2/utils/packet_parser.h"
#include "utils/status_macros.h"

namespace config {
namespace {

struct BoardChannelReference {
  std::string board_name;
  uint32_t channel;
};

struct DeviceResources {
  std::vector<BoardChannelReference> board_channels;
  std::vector<robot::comm::Comm> comms;
};

struct DeviceDependencies {
  std::string owner;
  ros2::node::Node node;
  DeviceResources resources;
};

struct Connection {
  std::string owner;
  uint32_t node_id;
  robot::comm::Comm comm;
};

absl::Status ValidateSensorConfig(const robot::perception::SinglePerception& sensor) {
  using robot::perception::SensorType;
  using robot::perception::SinglePerception;
  const std::string owner = absl::StrCat("Sensor '", sensor.sensor_name(), "'");
  if (sensor.sensor_name().empty()) {
    return absl::InvalidArgumentError("Sensor has no sensor_name.");
  }
  if (sensor.sensor_type() == SensorType::SENSOR_INVALID) {
    return absl::InvalidArgumentError(absl::StrCat(owner, " has no sensor_type."));
  }
  switch (sensor.sensor_config_case()) {
    case SinglePerception::kSts3215EncoderConfig: {
      if (sensor.sensor_type() != SensorType::POSITION) {
        return absl::InvalidArgumentError(
            absl::StrCat(owner, ": sts3215_encoder_config requires POSITION sensor_type."));
      }
      return absl::OkStatus();
    }
    case SinglePerception::kOpencvConfig:
      if (sensor.sensor_type() != SensorType::IMAGE) {
        return absl::InvalidArgumentError(
            absl::StrCat(owner, ": opencv_config requires IMAGE sensor_type."));
      }
      return absl::OkStatus();
    case SinglePerception::kLds01Config:
      if (sensor.sensor_type() != SensorType::RANGE_SCAN) {
        return absl::InvalidArgumentError(
            absl::StrCat(owner, ": lds01_config requires RANGE_SCAN sensor_type."));
      }
      if (!sensor.lds01_config().has_comm() ||
          sensor.lds01_config().comm().transport_type() != robot::comm::BYTE_STREAM) {
        return absl::InvalidArgumentError(
            absl::StrCat(owner, ": lds01_config requires BYTE_STREAM comm."));
      }
      return absl::OkStatus();
    case SinglePerception::SENSOR_CONFIG_NOT_SET:
    default:
      return absl::InvalidArgumentError(absl::StrCat(owner, " has no sensor_config."));
  }
}

absl::Status ValidateSensorConfigs(const robot::perception::Perception& perceptions) {
  for (const auto& sensor : perceptions.single_perceptions()) {
    ABSL_RETURN_IF_ERROR(ValidateSensorConfig(sensor));
  }
  return absl::OkStatus();
}

// Extract resources independently of validation. Sensor-specific field access is
// confined here; resource resolution and ownership checks stay generic.
absl::StatusOr<DeviceResources> CollectSensorDependencies(
    const robot::perception::SinglePerception& sensor) {
  using robot::perception::SinglePerception;
  switch (sensor.sensor_config_case()) {
    case SinglePerception::kSts3215EncoderConfig: {
      const auto& config = sensor.sts3215_encoder_config();
      return DeviceResources{{{config.board_name(), config.channel()}}, {}};
    }
    case SinglePerception::kLds01Config:
      return DeviceResources{{}, {sensor.lds01_config().comm()}};
    case SinglePerception::kOpencvConfig:
      return DeviceResources{};
    default:
      return absl::InvalidArgumentError("Cannot collect dependencies without a sensor config.");
  }
}

absl::StatusOr<std::vector<DeviceDependencies>> CollectDeviceDependencies(
    const config::Robot& robot) {
  std::vector<DeviceDependencies> devices;
  for (const auto& action : robot.actions().single_actions()) {
    if (!action.has_actuator()) continue;
    const auto& actuator = action.actuator();
    devices.push_back({absl::StrCat("Actuator '", actuator.actuator_name(), "'"),
                       action.node(),
                       {{{actuator.board_name(), actuator.channel()}}, {}}});
  }
  for (const auto& sensor : robot.perceptions().single_perceptions()) {
    ABSL_ASSIGN_OR_RETURN(auto resources, CollectSensorDependencies(sensor));
    devices.push_back(
        {absl::StrCat("Sensor '", sensor.sensor_name(), "'"), sensor.node(), std::move(resources)});
  }
  return devices;
}

absl::Status ValidateNodeAssignments(const std::vector<DeviceDependencies>& devices) {
  std::map<uint32_t, ros2::node::NodeType> node_types;
  for (const auto& device : devices) {
    const auto type = device.node.node_type();
    if (type == ros2::node::NODE_INVALID || !ros2::node::NodeType_IsValid(type)) {
      return absl::InvalidArgumentError(absl::StrCat(device.owner, " has an invalid node_type."));
    }
    const auto [it, inserted] = node_types.emplace(device.node.id(), type);
    if (!inserted && it->second != type) {
      return absl::InvalidArgumentError(
          absl::StrCat("Node ", device.node.id(), " has conflicting node types."));
    }
  }
  return absl::OkStatus();
}

// Validate wire contracts without constructing any driver or opening devices.
absl::Status ValidateNumericEndpoints(const config::Robot& robot) {
  std::map<std::string, ros2::data_type::Ros2DataType> topic_types;
  auto check_topic = [&](std::string topic, ros2::data_type::Ros2DataType type) {
    if (topic.empty()) return absl::InvalidArgumentError("Numeric endpoint needs a topic");
    if (topic.front() != '/') topic = "/" + topic;
    const auto [it, inserted] = topic_types.emplace(topic, type);
    if (!inserted && it->second != type)
      return absl::InvalidArgumentError("Conflicting message types on topic " + topic);
    return absl::OkStatus();
  };
  for (const auto& sensor : robot.perceptions().single_perceptions()) {
    if (sensor.node().node_type() != ros2::node::POSITION_PUBLISHER) continue;
    for (const auto& pub : sensor.node().publishers()) {
      ABSL_RETURN_IF_ERROR(ros2_utils::ValidatePositionMessageType(pub.ros2_data_type(), sensor));
      if (pub.publish_rate_hz() == 0)
        return absl::InvalidArgumentError("Position publisher requires a positive rate");
      ABSL_RETURN_IF_ERROR(check_topic(pub.topic(), pub.ros2_data_type()));
    }
  }
  for (const auto& action : robot.actions().single_actions()) {
    if (action.node().node_type() != ros2::node::ACTUATOR_SUBSCRIBER) continue;
    const auto& actuator = action.actuator();
    for (const auto& sub : action.node().subscriptions()) {
      ABSL_RETURN_IF_ERROR(ros2_utils::ValidateActionMessageType(sub, actuator));
      ABSL_RETURN_IF_ERROR(check_topic(sub.topic(), sub.ros2_data_type()));
      std::string field = "position";
      if (sub.ros2_data_type() != ros2::data_type::JOINT_STATE) {
        ABSL_ASSIGN_OR_RETURN(field, ros2_utils::ParseActionTypeFromTopic(sub.topic()));
      }
      if (field == "position" &&
          (!std::isfinite(actuator.operational_lower_limit()) ||
           !std::isfinite(actuator.operational_upper_limit()) ||
           actuator.operational_lower_limit() > actuator.operational_upper_limit() ||
           !std::isfinite(actuator.operational_upper_limit() - actuator.operational_lower_limit())))
        return absl::InvalidArgumentError("Invalid actuator operational limits");
    }
  }
  return absl::OkStatus();
}

// Every declared board reference is resolved, regardless of sensor measurement
// or driver type. Direct connections need no board reference.
absl::StatusOr<std::vector<Connection>> ResolveConnections(
    const google::protobuf::RepeatedPtrField<robot::board::Board>& boards,
    const std::vector<DeviceDependencies>& devices) {
  std::vector<Connection> connections;
  for (const auto& device : devices) {
    for (const auto& reference : device.resources.board_channels) {
      ABSL_ASSIGN_OR_RETURN(auto resolved,
                            robot::board::ResolveChannelConfig(
                                boards, device.owner, reference.board_name, reference.channel));
      connections.push_back({device.owner, device.node.id(), resolved.board->comm()});
    }
    for (const auto& comm : device.resources.comms) {
      connections.push_back({device.owner, device.node.id(), comm});
    }
  }
  return connections;
}

absl::Status ValidateSerialConnections(const std::vector<Connection>& connections) {
  std::map<std::string, robot::comm::SerialConfig> policies;
  for (const auto& connection : connections) {
    if (connection.comm.comm_type() != robot::comm::SERIAL) continue;
    const auto& serial = connection.comm.serial_config();
    auto status = robot::comm::CommFactory::ValidateSerialConfig(serial);
    if (!status.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(connection.owner, ": ", status.message()));
    }
    auto normalized = serial;
    normalized.clear_id();
    if (!normalized.has_exchange_timeout_ms()) normalized.set_exchange_timeout_ms(100);
    const auto [it, inserted] = policies.emplace(serial.port(), normalized);
    if (!inserted && it->second.SerializeAsString() != normalized.SerializeAsString())
      return absl::InvalidArgumentError("Serial port has conflicting baudrate/timing policy");
  }
  return absl::OkStatus();
}

absl::Status ValidateBusOwnership(const std::vector<Connection>& connections) {
  std::map<std::string, uint32_t> port_to_node_id;
  for (const auto& connection : connections) {
    // Both serial buses and EtherCAT NICs must have one node-process owner.
    const bool serial = connection.comm.comm_type() == robot::comm::SERIAL;
    const bool ethercat = connection.comm.comm_type() == robot::comm::ETHERCAT;
    if (!serial && !ethercat) continue;
    const std::string port =
        serial ? "Serial port '" + connection.comm.serial_config().port()
               : "EtherCAT NIC '" + connection.comm.ethercat_config().interface_name();
    const auto [it, inserted] = port_to_node_id.emplace(port, connection.node_id);
    if (!inserted && it->second != connection.node_id) {
      return absl::InvalidArgumentError(
          absl::StrCat(port,
                       "' is assigned to multiple node_ids (",
                       it->second,
                       " and ",
                       connection.node_id,
                       "); board consumers must run in the same process."));
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateEthercatBoards(
    const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
  std::map<std::string, std::string> policies;
  std::map<std::pair<std::string, uint32_t>, std::string> endpoints;
  for (const auto& board : boards) {
    const auto& comm = board.comm();
    if (comm.comm_type() != robot::comm::ETHERCAT) continue;
    const auto& ec = comm.ethercat_config();
    if (ec.interface_name().empty()) return absl::InvalidArgumentError("EtherCAT board has no NIC");
    const bool paired = comm.transport_type() == robot::comm::MESSAGE_AND_CYCLIC;
    if (paired) {
      ABSL_RETURN_IF_ERROR(robot::comm::CommFactory::ValidatePairedEthercatConfig(ec));
      if (board.protocol() != robot::board::JOSHUA_WIRE || board.has_am243_config())
        return absl::InvalidArgumentError(
            "Paired EtherCAT requires explicit JW and endpoint config in comm");
      if (!endpoints.emplace(std::make_pair(ec.interface_name(), ec.slave_index()), board.name())
               .second)
        return absl::InvalidArgumentError(
            "Multiple boards declare the same EtherCAT slave endpoint");
    } else {
      return absl::InvalidArgumentError(
          "Legacy TI-demo EtherCAT is retired; explicit JW with MESSAGE_AND_CYCLIC is required");
    }
    const std::string policy = std::to_string(comm.transport_type()) + ":" +
                               std::to_string(ec.process_data_mode()) + ":" +
                               ec.timing().SerializeAsString();
    const auto [it, added] = policies.emplace(ec.interface_name(), policy);
    if (!added && it->second != policy)
      return absl::InvalidArgumentError("EtherCAT NIC has conflicting profile or timing policies");
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ValidateConfig(const config::Config& config) {
  const auto& robot = config.robot();
  ABSL_RETURN_IF_ERROR(ValidateEthercatBoards(robot.boards()));
  for (const auto& board : robot.boards()) {
    if (board.has_am243_config())
      return absl::InvalidArgumentError(
          "Legacy am243_config is retired; endpoint fields belong in comm.ethercat_config");
  }
  for (const auto& action : robot.actions().single_actions()) {
    if (action.has_actuator() && (action.actuator().motor_type() == robot::action::MOTOR_TI_DEMO ||
                                  action.actuator().has_am243_ethercat_config()))
      return absl::InvalidArgumentError(
          "Legacy TI-demo motor/actuator config is retired; configure a supported motor");
  }
  ABSL_RETURN_IF_ERROR(ValidateNumericEndpoints(robot));
  ABSL_RETURN_IF_ERROR(ValidateSensorConfigs(robot.perceptions()));
  ABSL_ASSIGN_OR_RETURN(auto devices, CollectDeviceDependencies(robot));
  ABSL_RETURN_IF_ERROR(ValidateNodeAssignments(devices));
  ABSL_ASSIGN_OR_RETURN(auto connections, ResolveConnections(robot.boards(), devices));
  ABSL_RETURN_IF_ERROR(ValidateSerialConnections(connections));
  ABSL_RETURN_IF_ERROR(ValidateBusOwnership(connections));
  return absl::OkStatus();
}
}  // namespace config
