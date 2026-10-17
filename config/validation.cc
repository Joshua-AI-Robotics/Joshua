#include "config/validation.h"

#include <arpa/inet.h>

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "robot/board/factory/board_resolver.h"
#include "robot/perception/audio/audio_config.h"
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
    case SinglePerception::kAlsaAudioConfig:
      if (sensor.sensor_type() != SensorType::AUDIO ||
          sensor.node().node_type() != ros2::node::AUDIO_PUBLISHER) {
        return absl::InvalidArgumentError("ALSA requires AUDIO and AUDIO_PUBLISHER");
      }
      if (sensor.node().publishers().empty() || !sensor.node().subscriptions().empty())
        return absl::InvalidArgumentError("Audio requires publishers and no subscriptions");
      for (const auto& pub : sensor.node().publishers()) {
        if (pub.topic().empty() || pub.ros2_data_type() != ros2::data_type::UINT8_MULTI_ARRAY ||
            pub.publish_rate_hz() != 0)
          return absl::InvalidArgumentError(
              "Audio publishes protobuf packets as UINT8_MULTI_ARRAY; rate is determined by sample "
              "rate / chunk frames (publish_rate_hz must be 0)");
      }
      return robot::perception::ValidateAudioConfig(sensor.alsa_audio_config());
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
  std::set<std::string> audio_devices;
  for (const auto& sensor : perceptions.single_perceptions()) {
    if ((sensor.sensor_type() == robot::perception::AUDIO ||
         sensor.node().node_type() == ros2::node::AUDIO_PUBLISHER) &&
        !sensor.has_alsa_audio_config())
      return absl::InvalidArgumentError("Audio sensor/node requires alsa_audio_config");
    if (sensor.has_alsa_audio_config() &&
        !audio_devices.insert(sensor.alsa_audio_config().device()).second)
      return absl::InvalidArgumentError("Audio device must have a single capture owner");
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
    case SinglePerception::kAlsaAudioConfig:
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

absl::Status ValidateMonitorModels(const config::Config& config) {
  std::set<uint32_t> ids;
  for (const auto& sensor : config.robot().perceptions().single_perceptions())
    ids.insert(sensor.node().id());
  for (const auto& action : config.robot().actions().single_actions())
    ids.insert(action.node().id());
  for (const auto& model : config.ai().models().single_models()) {
    const bool audio = model.model_type() == ai::models::YAMNET;
    const bool video = model.model_type() == ai::models::VIDEO_DETECTION;
    const auto type = model.node().node_type();
    if (!audio && !video && type != ros2::node::YAMNET_NODE &&
        type != ros2::node::VIDEO_INFERENCE_NODE)
      continue;
    if (!ids.insert(model.node().id()).second)
      return absl::InvalidArgumentError("Monitor node ID conflicts with another node");
    if ((audio && type != ros2::node::YAMNET_NODE) ||
        (video && type != ros2::node::VIDEO_INFERENCE_NODE) || (!audio && !video))
      return absl::InvalidArgumentError("Monitor model and node type mismatch");
    const auto& node = model.node();
    if (node.subscriptions_size() != 1 || node.publishers_size() != 1 ||
        node.subscriptions(0).topic().empty() || node.publishers(0).topic().empty() ||
        node.subscriptions(0).topic() == node.publishers(0).topic() ||
        node.publishers(0).ros2_data_type() != ros2::data_type::STRING ||
        node.subscriptions(0).ros2_data_type() !=
            (audio ? ros2::data_type::UINT8_MULTI_ARRAY : ros2::data_type::IMAGE))
      return absl::InvalidArgumentError(
          "Monitor requires one typed input and one distinct STRING event output");
    if (audio) {
      const auto& c = model.yamnet_config();
      if (!(c.cry_threshold() > 0 && c.cry_threshold() <= 1) || c.persistence_windows() < 1 ||
          c.persistence_windows() > 100 || c.num_threads() < 1 || c.num_threads() > 8)
        return absl::InvalidArgumentError("Invalid YAMNet settings");
    } else {
      const auto& c = model.video_detection_config();
      if (!(c.score_threshold() > 0 && c.score_threshold() <= 1) ||
          !(c.inference_rate_hz() > 0 && c.inference_rate_hz() <= 15) ||
          !(c.motion_fraction_threshold() > 0 && c.motion_fraction_threshold() <= 1) ||
          c.num_threads() < 1 || c.num_threads() > 8)
        return absl::InvalidArgumentError("Invalid video detection settings");
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateMediaStreams(const config::Config& config) {
  std::set<uint32_t> ids;
  for (const auto& s : config.robot().perceptions().single_perceptions()) ids.insert(s.node().id());
  for (const auto& a : config.robot().actions().single_actions()) ids.insert(a.node().id());
  for (const auto& m : config.ai().models().single_models()) ids.insert(m.node().id());
  for (const auto& d : config.ai().data_stores().single_data_stores()) ids.insert(d.node().id());
  for (const auto& t : config.robot().trajectories().single_trajectories())
    ids.insert(t.node().id());
  std::set<uint32_t> ports;
  for (const auto& s : config.utilities().media_streams()) {
    const auto& n = s.node();
    if (!ids.insert(n.id()).second || n.node_type() != ros2::node::MEDIA_STREAM_NODE)
      return absl::InvalidArgumentError("Media stream requires a unique MEDIA_STREAM_NODE id");
    if (s.width() < 16 || s.width() > 1920 || s.width() % 2 || s.height() < 16 ||
        s.height() > 1080 || s.height() % 2 || s.fps() < 1 || s.fps() > 30 ||
        s.bitrate_kbps() < 100 || s.bitrate_kbps() > 10000 || s.stream_name().empty() ||
        s.stream_name().find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
            std::string::npos)
      return absl::InvalidArgumentError("Invalid media dimensions, FPS, bitrate, or stream name");
    for (auto port : {s.http_port(), s.ice_port(), s.rtsp_port()}) {
      if (port < 1024 || port > 65535 || !ports.insert(port).second)
        return absl::InvalidArgumentError(
            "Media ports must be distinct and between 1024 and 65535");
    }
    if (!s.listen_address().empty()) {
      in_addr address{};
      if (inet_pton(AF_INET, s.listen_address().c_str(), &address) != 1)
        return absl::InvalidArgumentError(
            "Media listener must be a Tailscale or loopback IPv4 address");
      auto ip = ntohl(address.s_addr);
      if ((ip >> 24) != 127 && (ip & 0xffc00000U) != 0x64400000U)
        return absl::InvalidArgumentError(
            "Media listener must be a Tailscale or loopback IPv4 address");
    }
    int images = 0, audio = 0;
    std::set<std::string> topics;
    for (const auto& sub : n.subscriptions()) {
      if (sub.topic().empty() || !topics.insert(sub.topic()).second)
        return absl::InvalidArgumentError("Media input topics must be nonempty and distinct");
      if (sub.ros2_data_type() == ros2::data_type::IMAGE)
        ++images;
      else if (sub.ros2_data_type() == ros2::data_type::UINT8_MULTI_ARRAY)
        ++audio;
      else
        return absl::InvalidArgumentError("Media inputs must be IMAGE or UINT8_MULTI_ARRAY");
    }
    if (images != 1 || audio > 1 || n.publishers_size() != 1 ||
        n.publishers(0).ros2_data_type() != ros2::data_type::STRING ||
        n.publishers(0).topic().empty() || topics.count(n.publishers(0).topic()))
      return absl::InvalidArgumentError(
          "Media requires one image, optional audio, and a distinct STRING status output");
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ValidateConfig(const config::Config& config) {
  ABSL_RETURN_IF_ERROR(ValidateMediaStreams(config));
  ABSL_RETURN_IF_ERROR(ValidateMonitorModels(config));
  const auto& robot = config.robot();
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
