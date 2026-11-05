#include <gflags/gflags.h>
#include <glog/logging.h>

#include <string>
#include <utility>
#include <vector>

#include "config/config_utils.h"
#include "config/validation.h"
#include "launcher/simulation_launcher.h"
#include "node_generator/node_generator.h"
#include "utils/robot_time.h"
#include "version.h"

DEFINE_string(config,
              "config/config_preset/so100/teleoperate.pbtxt",
              "Path to the robot config file");

DEFINE_string(observation_dir, "", "Existing private passive snapshot directory (opt-in)");
DEFINE_string(observe_sensor, "", "One configured POSITION/JOINT_STATE sensor to observe");

int main(int argc, char* argv[]) {
  gflags::SetVersionString(JOSHUA_VERSION);
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  // Preserve idiomatic gflags names and the documented hyphenated aliases.
  std::vector<std::string> flag_storage;
  for (int i = 0; i < argc; ++i) {
    std::string flag(argv[i]);
    for (const auto& alias :
         {std::pair<std::string, std::string>{"--observation-dir", "--observation_dir"},
          {"--observe-sensor", "--observe_sensor"}})
      if (flag == alias.first || flag.rfind(alias.first + "=", 0) == 0)
        flag.replace(0, alias.first.size(), alias.second);
    flag_storage.push_back(std::move(flag));
  }
  std::vector<char*> flag_pointers;
  for (auto& flag : flag_storage) flag_pointers.push_back(flag.data());
  flag_pointers.push_back(nullptr);
  argv = flag_pointers.data();
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  LOG(INFO) << "Starting Joshua Platform with config: " << FLAGS_config;

  auto config_or = config::config_util::LoadConfig(FLAGS_config);
  if (!config_or.ok()) {
    LOG(ERROR) << "Failed to load config: " << config_or.status();
    return 1;
  }
  const auto& config = config_or.value();

  const auto valid = config::ValidateConfig(config);
  if (!valid.ok()) {
    LOG(ERROR) << "Invalid config: " << valid;
    return 1;
  }
  auto clock = joshua::MakeRobotClock(config.general().robot_clock());
  if (!clock.ok()) {
    LOG(ERROR) << "Failed to create robot clock: " << clock.status();
    return 1;
  }
  const std::string clock_source((*clock)->SourceName());
  const auto installed = joshua::SetGlobalRobotClock(std::move(*clock));
  if (!installed.ok()) {
    LOG(ERROR) << "Failed to install robot clock: " << installed;
    return 1;
  }
  LOG(INFO) << "Robot clock initialized: " << clock_source;

  if (!FLAGS_observe_sensor.empty() && FLAGS_observation_dir.empty()) {
    LOG(ERROR) << "--observe_sensor requires --observation_dir";
    return 1;
  }
  if (config.general().operation_mode() == config::General::MODE_SIMULATION) {
    if (!FLAGS_observation_dir.empty()) {
      LOG(ERROR) << "Simulation passive observation is unsupported.";
      return 1;
    }
    return launcher::RunSimulation(FLAGS_config, config);
  }

  try {
    node_generator::NodeGenerator node_generator(FLAGS_config);

    if (const auto status = node_generator.Initialize(&config); !status.ok()) {
      LOG(ERROR) << "Failed to initialize NodeGenerator: " << status;
      return 1;
    }

    if (!FLAGS_observation_dir.empty()) {
      auto status =
          node_generator.ConfigureObservation(FLAGS_observation_dir, FLAGS_observe_sensor);
      if (!status.ok()) {
        LOG(ERROR) << status;
        return 1;
      }
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
