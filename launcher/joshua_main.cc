#include <gflags/gflags.h>
#include <glog/logging.h>

#include "launcher/simulation_launcher.h"
#include "node_generator/node_manager.h"
#include "utils/joshua_runtime.h"
#include "version.h"

DEFINE_string(config,
              "config/config_preset/so100/teleoperate.pbtxt",
              "Path to the robot config file");

JOSHUA_MAIN(argc, argv) {
  gflags::SetVersionString(JOSHUA_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  node_generator::NodeManager manager(FLAGS_config);
  joshua::ThrowIfError(manager.Initialize());
  JoshuaInit(manager.config());
  LOG(INFO) << "Starting Joshua Platform with config: " << manager.config_path();

  if (manager.config().general().operation_mode() == config::General::MODE_SIMULATION)
    return launcher::RunSimulation(manager);

  const auto launched = manager.LaunchAllNodes();
  if (absl::IsCancelled(launched) && manager.ShutdownRequested()) return 0;
  joshua::ThrowIfError(launched);
  joshua::ThrowIfError(manager.MonitorNodes());
  return 0;
}
