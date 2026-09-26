#include <csignal>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

#include "config/config_utils.h"
#include "config/validation.h"
#include "rclcpp/rclcpp.hpp"

namespace ros2_utils {
// Each executable links one node implementation providing this factory.
std::shared_ptr<rclcpp::Node> CreateNode(const std::string& node_name,
                                         int node_id,
                                         const config::Config& config);

namespace {
void SigtermHandler(int) noexcept {
  rclcpp::shutdown();
}
}  // namespace

std::shared_ptr<rclcpp::Node> CreateValidatedNode(const std::string& node_name,
                                                  int node_id,
                                                  const config::Config& config) {
  const auto validation = config::ValidateConfig(config);
  if (!validation.ok()) throw std::invalid_argument(validation.ToString());
  return CreateNode(node_name, node_id, config);
}

int RunNode(int argc, char* argv[]) {
  const char* logger_name = "node_runner";
  int result = 0;
  try {
    rclcpp::init(argc, argv);
    std::signal(SIGTERM, SigtermHandler);
    if (argc < 4) {
      throw std::invalid_argument(std::string("Usage: ") + logger_name +
                                  " <node_name> <node_id> <config_path>");
    }
    const int node_id = std::stoi(argv[2]);
    auto loaded_config = config::config_util::LoadConfig(argv[3]);
    if (!loaded_config.ok()) throw std::invalid_argument(loaded_config.status().ToString());
    // Validate here even when launched without node_generator.
    rclcpp::spin(CreateValidatedNode(argv[1], node_id, *loaded_config));
  } catch (const std::exception& error) {
    RCLCPP_ERROR(rclcpp::get_logger(logger_name), "%s", error.what());
    result = 1;
  }
  if (rclcpp::ok()) rclcpp::shutdown();
  return result;
}

}  // namespace ros2_utils

#ifndef JOSHUA_NODE_TEST
int main(int argc, char* argv[]) {
  return ros2_utils::RunNode(argc, argv);
}
#endif
