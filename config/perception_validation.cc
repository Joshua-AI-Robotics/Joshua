#include "config/perception_validation.h"

#include <map>
#include <string>

#include "absl/strings/str_cat.h"
#include "robot/board/factory/board_resolver.h"
#include "robot/perception/factory/sensor_config.h"
#include "utils/status_macros.h"

namespace config::config_util {
absl::Status ValidatePerceptions(const Robot& robot) {
  std::map<std::string, uint32_t> port_to_node_id;
  auto claim_port = [&](const robot::comm::Comm& comm, uint32_t node_id) -> absl::Status {
    if (comm.comm_type() != robot::comm::SERIAL) return absl::OkStatus();
    const auto& port = comm.serial_config().port();
    if (port.empty() || comm.serial_config().baudrate() == 0) {
      return absl::InvalidArgumentError("Serial comm requires a port and baudrate.");
    }
    const auto [it, inserted] = port_to_node_id.emplace(port, node_id);
    if (!inserted && it->second != node_id) {
      return absl::InvalidArgumentError(
          absl::StrCat("Serial port '",
                       port,
                       "' is assigned to multiple node_ids (",
                       it->second,
                       " and ",
                       node_id,
                       "); board consumers must run in the same process."));
    }
    return absl::OkStatus();
  };
  for (const auto& action : robot.actions().single_actions()) {
    if (!action.has_actuator()) continue;
    const auto& actuator = action.actuator();
    ABSL_ASSIGN_OR_RETURN(
        auto resolved,
        robot::board::ResolveChannelConfig(
            robot.boards(), actuator.actuator_name(), actuator.board_name(), actuator.channel()));
    ABSL_RETURN_IF_ERROR(claim_port(resolved.board->comm(), action.node().id()));
  }
  for (const auto& sensor : robot.perceptions().single_perceptions()) {
    ABSL_RETURN_IF_ERROR(robot::perception::ValidateSensorConfig(sensor, robot.boards()));
    const auto node_type = sensor.node().node_type();
    const bool supported_node = (sensor.sensor_type() == robot::perception::POSITION &&
                                 (node_type == ros2::node::ENCODER_PUBLISHER ||
                                  node_type == ros2::node::ACTUATOR_SUBSCRIBER)) ||
                                (sensor.sensor_type() == robot::perception::IMAGE &&
                                 node_type == ros2::node::CAMERA_PUBLISHER) ||
                                (sensor.sensor_type() == robot::perception::RANGE_SCAN &&
                                 node_type == ros2::node::LIDAR_PUBLISHER);
    if (!supported_node) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Sensor '", sensor.sensor_name(), "' has an incompatible publisher node_type."));
    }
    const robot::comm::Comm* comm = nullptr;
    switch (sensor.sensor_config_case()) {
      case robot::perception::SinglePerception::kSts3215EncoderConfig: {
        const auto& config = sensor.sts3215_encoder_config();
        ABSL_ASSIGN_OR_RETURN(
            auto resolved,
            robot::board::ResolveChannelConfig(
                robot.boards(), sensor.sensor_name(), config.board_name(), config.channel()));
        comm = &resolved.board->comm();
        break;
      }
      case robot::perception::SinglePerception::kLds01Config:
        comm = &sensor.lds01_config().comm();
        break;
      case robot::perception::SinglePerception::kOpencvConfig:
        // OpenCV owns camera access; its device index is not a serial port.
        break;
      default:
        break;  // ValidateSensorConfig rejects missing or unknown drivers.
    }
    if (comm != nullptr) {
      ABSL_RETURN_IF_ERROR(claim_port(*comm, sensor.node().id()));
    }
  }
  return absl::OkStatus();
}
}  // namespace config::config_util
