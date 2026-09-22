// Private newline-delimited JSON IPC, not an implementation of MCP.
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>

#include "config/config_utils.h"
#include "google/protobuf/util/json_util.h"
#include "mhs/ros_client.h"
#include "ros2/actuator_session.h"

namespace {
volatile std::sig_atomic_t shutdown_requested = 0;
void RequestShutdown(int) {
  shutdown_requested = 1;
}
}  // namespace

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  setenv("RCUTILS_LOGGING_USE_STDOUT", "0", 1);
  if (argc != 2 && (argc != 3 || std::string(argv[2]) != "--connect-ros")) {
    std::cerr << "Usage: ros_bridge CONFIG [--connect-ros]\n";
    return 2;
  }
  // A broken adapter pipe must unwind through the ROS shutdown stop request.
  std::signal(SIGPIPE, SIG_IGN);
  std::signal(SIGTERM, RequestShutdown);
  std::signal(SIGINT, RequestShutdown);
  auto config = config::config_util::LoadConfig(argv[1]);
  if (!config.ok()) {
    std::cerr << config.status() << '\n';
    return 2;
  }
  auto device = ros2_actuator::ResolveDevice(*config);
  if (!device.ok()) {
    std::cerr << device.status() << '\n';
    return 2;
  }
  ros2_actuator::ActuatorSession offline(*device);
  std::unique_ptr<mhs::RosClient> client;
  if (argc == 3) {
    rclcpp::init(0, nullptr, rclcpp::InitOptions(), rclcpp::SignalHandlerOptions::None);
    client = std::make_unique<mhs::RosClient>(*device);
  }
  std::string pending;
  while (!shutdown_requested) {
    pollfd input{STDIN_FILENO, POLLIN, 0};
    const int ready = poll(&input, 1, 100);
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (ready == 0) continue;
    char buffer[4096];
    const auto count = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (count <= 0) break;
    pending.append(buffer, count);
    if (pending.size() > 65536) {
      std::cerr << "IPC request too large\n";
      break;
    }
    size_t newline;
    while ((newline = pending.find('\n')) != std::string::npos && !shutdown_requested) {
      const auto line = pending.substr(0, newline);
      pending.erase(0, newline + 1);
      google::protobuf::Struct request, response;
      const auto parsed = google::protobuf::util::JsonStringToMessage(line, &request);
      if (!parsed.ok()) {
        (*response.mutable_fields())["error"].set_string_value("Invalid request JSON object");
      } else {
        response = client ? client->Request(request) : offline.Handle(request);
      }
      std::string json;
      const auto encoded = google::protobuf::util::MessageToJsonString(response, &json);
      if (!encoded.ok()) json = "{\"error\":\"Response encoding failed\"}";
      std::cout << json << std::endl;
      if (!std::cout) {
        shutdown_requested = 1;
        break;
      }
    }
  }
  if (client) {
    auto stopped = client->Stop();
    const auto ack = stopped.fields().find("disable_acknowledged");
    if (ack == stopped.fields().end() || !ack->second.bool_value()) {
      std::cerr << "ROS shutdown disable unconfirmed\n";
      client.reset();
      rclcpp::shutdown();
      return 1;
    }
    client.reset();
    rclcpp::shutdown();
  }
  return 0;
}
