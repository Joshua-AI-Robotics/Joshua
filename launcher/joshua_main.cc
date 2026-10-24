#include <gflags/gflags.h>
#include <glog/logging.h>

#include "config/config_utils.h"
#include "config/validation.h"
#include "launcher/simulation_launcher.h"
#include "node_generator/node_generator.h"
#include "node_generator/process_supervisor.h"
#include "utils/joshua_runtime.h"
#include "version.h"

DEFINE_string(config,
              "config/config_preset/so100/teleoperate.pbtxt",
              "Path to the robot config file");

JOSHUA_MAIN(argc, argv) {
  node_generator::SupervisorSignals signals;
  gflags::SetVersionString(JOSHUA_VERSION);
  FLAGS_logtostderr = 1;
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  LOG(INFO) << "Starting Joshua Platform with config: " << FLAGS_config;

  auto config_or = config::config_util::LoadConfig(FLAGS_config);
  if (!config_or.ok()) {
    LOG(ERROR) << "Failed to load config: " << FLAGS_config;
    return 1;
  }
  const auto& config = config_or.value();

  const auto valid = config::ValidateConfig(config);
  if (!valid.ok()) {
    LOG(ERROR) << "Invalid config: " << valid;
    return 1;
  }
  JoshuaInit(config);

  if (config.general().operation_mode() == config::General::MODE_SIMULATION) {
    return launcher::RunSimulation(FLAGS_config, config);
  }

  node_generator::NodeGenerator generator(FLAGS_config);
  joshua::ThrowIfError(generator.Initialize());
  const auto launched = generator.LaunchAllNodes();
  if (absl::IsCancelled(launched) && signals.Requested()) return 0;
  joshua::ThrowIfError(launched);
  joshua::ThrowIfError(generator.MonitorNodes());

  LOG(INFO) << "Joshua Platform completed successfully";
  return 0;
}
