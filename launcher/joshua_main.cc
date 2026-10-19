#include <gflags/gflags.h>
#include <glog/logging.h>

#include "config/config_utils.h"
#include "launcher/simulation_launcher.h"
#include "node_generator/node_generator.h"
#include "version.h"

DEFINE_string(config,
              "config/config_preset/so100/teleoperate.pbtxt",
              "Path to the robot config file");

int main(int argc, char* argv[]) {
  gflags::SetVersionString(JOSHUA_VERSION);
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  LOG(INFO) << "Starting Joshua Platform with config: " << FLAGS_config;

  auto config_or = config::config_util::LoadConfig(FLAGS_config);
  if (!config_or.ok()) {
    LOG(ERROR) << "Failed to load config: " << config_or.status();
    return 1;
  }
  const auto& config = config_or.value();

  if (config.general().operation_mode() == config::General::MODE_SIMULATION) {
    return launcher::RunSimulation(FLAGS_config, config);
  }

  try {
    node_generator::NodeGenerator node_generator(FLAGS_config);

    if (const auto status = node_generator.Initialize(); !status.ok()) {
      LOG(ERROR) << "Failed to initialize NodeGenerator: " << status;
      return 1;
    }

    if (const auto status = node_generator.LaunchAllNodes(); !status.ok()) {
      LOG(ERROR) << "Failed to launch nodes: " << status;
      return 1;
    }

    if (!node_generator.has_nodes()) {
      LOG(WARNING) << "No nodes were launched successfully";
      return 1;
    }

    auto res = node_generator.MonitorNodes();
    if (!res.ok()) {
      LOG(ERROR) << "Failed to monitor nodes: " << res;
      return 1;
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "NodeGenerator error: " << e.what();
    return 1;
  }

  LOG(INFO) << "Joshua Platform completed successfully";
  return 0;
}
