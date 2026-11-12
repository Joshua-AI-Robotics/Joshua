#include <gflags/gflags.h>
#include <glog/logging.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "launcher/node_manager/node_manager.h"
#include "launcher/simulation_launcher.h"
#include "utils/joshua_runtime.h"
#include "version.h"

DEFINE_string(config,
              "config/config_preset/so100/teleoperate.pbtxt",
              "Path to the robot config file");

DEFINE_string(observation_dir, "", "Existing private passive snapshot directory (opt-in)");
DEFINE_string(observe_sensor, "", "One configured POSITION/JOINT_STATE sensor to observe");

JOSHUA_MAIN(argc, argv) {
  gflags::SetVersionString(JOSHUA_VERSION);
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

  launcher::NodeManager manager(FLAGS_config);
  joshua::ThrowIfError(manager.Initialize());
  JoshuaInit(manager.config());
  LOG(INFO) << "Starting Joshua Platform with config: " << manager.config_path();

  if (!FLAGS_observe_sensor.empty() && FLAGS_observation_dir.empty())
    throw std::invalid_argument("--observe_sensor requires --observation_dir");
  if (manager.config().general().operation_mode() == config::General::MODE_SIMULATION) {
    if (!FLAGS_observation_dir.empty())
      throw std::invalid_argument("Simulation passive observation is unsupported");
    return launcher::RunSimulation(manager);
  }
  if (!FLAGS_observation_dir.empty())
    joshua::ThrowIfError(manager.ConfigureObservation(FLAGS_observation_dir, FLAGS_observe_sensor));

  const auto launched = manager.LaunchAllNodes();
  if (absl::IsCancelled(launched) && manager.ShutdownRequested()) return 0;
  joshua::ThrowIfError(launched);
  joshua::ThrowIfError(manager.MonitorNodes());
  return 0;
}
