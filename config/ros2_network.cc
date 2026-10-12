#include "config/ros2_network.h"

#include <arpa/inet.h>
#include <unistd.h>

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "glog/logging.h"

namespace config {
namespace {
using Network = Ros2NetworkConfig;

std::string Env(const char* name) {
  const char* value = std::getenv(name);
  return value ? value : "";
}

uint32_t ScanCount(const Network& config) {
  return config.has_participant_scan_count() ? config.participant_scan_count() : 32;
}

absl::Status Invalid(const std::string& message) {
  return absl::InvalidArgumentError("general.ros2_network: " + message);
}

std::string ReadFile(const std::string& path) {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string Profile(const Network& config, uint32_t domain) {
  std::ostringstream xml;
  xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<profiles xmlns=\"http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles\">\n"
      << "<participant profile_name=\"joshua_network\" is_default_profile=\"true\">"
      << "<rtps><builtin>";
  if (config.discovery() == Network::STATIC_PEERS) {
    // An explicit unicast listener prevents Fast DDS from supplying a default
    // multicast listener. Port zero selects this participant's well-known port.
    xml << "<metatrafficUnicastLocatorList><locator><udpv4><address>0.0.0.0</address>"
        << "<port>0</port></udpv4></locator></metatrafficUnicastLocatorList>"
        << "<initialPeersList>";
    std::set<std::string> peers(config.peers().begin(), config.peers().end());
    peers.insert("127.0.0.1");
    for (const auto& peer : peers) {
      for (uint32_t id = 0; id < ScanCount(config); ++id) {
        xml << "<locator><udpv4><address>" << peer << "</address><port>"
            << (7410 + 250 * domain + 2 * id) << "</port></udpv4></locator>";
      }
    }
    xml << "</initialPeersList>";
  }
  xml << "</builtin></rtps></participant></profiles>\n";
  return xml.str();
}
}  // namespace

absl::Status ValidateRos2Network(const Network& config) {
  if (!Network::Discovery_IsValid(config.discovery())) return Invalid("unknown discovery mode");
  // Conservative Linux ROS domain range. Static discovery also checks that
  // its participant range stays below the default ephemeral UDP ports.
  if (config.has_domain_id() && config.domain_id() > 101) {
    return Invalid("domain_id must be in [0, 101]");
  }
  if (config.discovery() != Network::STATIC_PEERS) {
    if (!config.peers().empty() || config.has_participant_scan_count()) {
      return Invalid("peers and participant_scan_count require STATIC_PEERS");
    }
    return absl::OkStatus();
  }
  if (config.peers().empty()) return Invalid("STATIC_PEERS requires at least one peer");
  if (ScanCount(config) == 0 || ScanCount(config) > 120) {
    return Invalid("participant_scan_count must be in [1, 120]");
  }
  if (config.has_domain_id() &&
      7411 + 250 * config.domain_id() + 2 * (ScanCount(config) - 1) >= 32768) {
    return Invalid("domain_id and participant_scan_count overlap Linux ephemeral UDP ports");
  }
  for (const auto& peer : config.peers()) {
    in_addr address{};
    if (inet_pton(AF_INET, peer.c_str(), &address) != 1 || peer.find('\0') != std::string::npos) {
      return Invalid("peer must be a unicast IPv4 address: " + peer);
    }
    const auto ip = ntohl(address.s_addr);
    if ((ip >> 24) == 0 || (ip >> 24) >= 224) {
      return Invalid("peer must be a unicast IPv4 address: " + peer);
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<Ros2NetworkEnvironment>> Ros2NetworkEnvironment::Prepare(
    const Network& config, const std::string& output_parent, bool reuse_profile) {
  const auto valid = ValidateRos2Network(config);
  if (!valid.ok()) return valid;
  auto result = std::unique_ptr<Ros2NetworkEnvironment>(new Ros2NetworkEnvironment);
  result->owner_pid_ = getpid();
  if (config.has_domain_id()) {
    result->variables_["ROS_DOMAIN_ID"] = std::to_string(config.domain_id());
  }
  if (config.discovery() == Network::INHERIT) return result;

  uint32_t domain = config.domain_id();
  if (!config.has_domain_id() && !Env("ROS_DOMAIN_ID").empty()) {
    const auto text = Env("ROS_DOMAIN_ID");
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), domain);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || domain > 101) {
      return Invalid("inherited ROS_DOMAIN_ID must be in [0, 101]");
    }
  }
  auto effective = config;
  effective.set_domain_id(domain);
  const auto effective_valid = ValidateRos2Network(effective);
  if (!effective_valid.ok()) return effective_valid;
  const auto rmw = Env("RMW_IMPLEMENTATION");
  if (!rmw.empty() && rmw != "rmw_fastrtps_cpp") {
    return Invalid(
        "managed discovery requires rmw_fastrtps_cpp; unset RMW_IMPLEMENTATION "
        "or use INHERIT");
  }
  for (const auto* name : {"ROS_DISCOVERY_SERVER", "ROS_SUPER_CLIENT", "ROS_STATIC_PEERS"}) {
    if (!Env(name).empty())
      return Invalid(std::string("unset conflicting ") + name + " or use INHERIT");
  }

  const auto xml = Profile(config, domain);
  std::string profile_path;
  for (const auto* name : {"FASTRTPS_DEFAULT_PROFILES_FILE", "FASTDDS_DEFAULT_PROFILES_FILE"}) {
    const auto path = Env(name);
    if (path.empty()) continue;
    if (ReadFile(path) != xml) {
      return Invalid(std::string("unset conflicting ") + name + " or use INHERIT");
    }
    std::error_code path_error;
    profile_path = std::filesystem::absolute(path, path_error).string();
    if (path_error) return Invalid("cannot resolve inherited profile path: " + path);
  }
  if (!reuse_profile) profile_path.clear();
  if (profile_path.empty()) {
    std::error_code error;
    auto parent = output_parent.empty() ? std::filesystem::temp_directory_path(error)
                                        : std::filesystem::absolute(output_parent, error);
    if (error)
      return absl::InternalError("Cannot resolve network profile directory: " + error.message());
    auto pattern = (parent / "joshua-ros2-XXXXXX").string();
    if (!mkdtemp(pattern.data()))
      return absl::InternalError("Cannot create network profile directory in " + parent.string());
    result->directory_ = pattern;
    profile_path = (std::filesystem::path(pattern) / "fastdds.xml").string();
    std::ofstream file(profile_path);
    file << xml;
    file.close();
    if (!file) return absl::InternalError("Cannot write network profile: " + profile_path);
  }
  auto& env = result->variables_;
  env["ROS_DOMAIN_ID"] = std::to_string(domain);
  env["RMW_IMPLEMENTATION"] = "rmw_fastrtps_cpp";
  env["ROS_LOCALHOST_ONLY"] = config.discovery() == Network::LOCALHOST ? "1" : "0";
  // Jazzy SUBNET appends multicast initial peers even when XML supplies peers.
  // SYSTEM_DEFAULT leaves our unicast discovery profile intact; Humble ignores it.
  env["ROS_AUTOMATIC_DISCOVERY_RANGE"] = config.discovery() == Network::LOCALHOST ? "LOCALHOST"
                                         : config.discovery() == Network::STATIC_PEERS
                                             ? "SYSTEM_DEFAULT"
                                             : "SUBNET";
  env["FASTRTPS_DEFAULT_PROFILES_FILE"] = profile_path;
  env["FASTDDS_DEFAULT_PROFILES_FILE"] = profile_path;
  LOG(INFO) << "ROS network: domain=" << domain
            << " discovery=" << Network::Discovery_Name(config.discovery())
            << " peers=" << config.peers_size() << " profile=" << profile_path;
  return result;
}

absl::Status Ros2NetworkEnvironment::Apply() {
  if (!previous_.empty()) return absl::OkStatus();
  for (const auto& [key, value] : variables_) {
    const char* old = std::getenv(key.c_str());
    previous_[key] = old ? std::optional<std::string>(old) : std::nullopt;
    if (old && old != value) LOG(INFO) << "ROS network config overrides " << key;
    if (setenv(key.c_str(), value.c_str(), 1) != 0) {
      return absl::InternalError("Cannot set " + key);
    }
  }
  return absl::OkStatus();
}

Ros2NetworkEnvironment::~Ros2NetworkEnvironment() {
  if (owner_pid_ != getpid()) return;
  for (const auto& [key, value] : previous_) {
    if (value)
      setenv(key.c_str(), value->c_str(), 1);
    else
      unsetenv(key.c_str());
  }
  if (!directory_.empty()) {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }
}
}  // namespace config
