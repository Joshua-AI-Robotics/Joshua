#include "config/validation.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "robot/board/factory/board_resolver.h"
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
  for (const auto& connection : connections) {
    if (connection.comm.comm_type() != robot::comm::SERIAL) continue;
    const auto& serial = connection.comm.serial_config();
    if (serial.port().empty() || serial.baudrate() == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat(connection.owner, ": serial comm requires a port and baudrate."));
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateBusOwnership(const std::vector<Connection>& connections) {
  std::map<std::string, uint32_t> port_to_node_id;
  for (const auto& connection : connections) {
    // Serial ports are process-owned. Other transports can add their own
    // ownership rules here without introducing sensor-specific cases.
    if (connection.comm.comm_type() != robot::comm::SERIAL) continue;
    const auto& port = connection.comm.serial_config().port();
    const auto [it, inserted] = port_to_node_id.emplace(port, connection.node_id);
    if (!inserted && it->second != connection.node_id) {
      return absl::InvalidArgumentError(
          absl::StrCat("Serial port '",
                       port,
                       "' is assigned to multiple node_ids (",
                       it->second,
                       " and ",
                       connection.node_id,
                       "); board consumers must run in the same process."));
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ValidateConfig(const config::Config& config) {
  const auto& robot = config.robot();
  ABSL_RETURN_IF_ERROR(ValidateSensorConfigs(robot.perceptions()));
  ABSL_ASSIGN_OR_RETURN(auto devices, CollectDeviceDependencies(robot));
  ABSL_RETURN_IF_ERROR(ValidateNodeAssignments(devices));
  ABSL_ASSIGN_OR_RETURN(auto connections, ResolveConnections(robot.boards(), devices));
  ABSL_RETURN_IF_ERROR(ValidateSerialConnections(connections));
  ABSL_RETURN_IF_ERROR(ValidateBusOwnership(connections));
  return absl::OkStatus();
}
}  // namespace config
