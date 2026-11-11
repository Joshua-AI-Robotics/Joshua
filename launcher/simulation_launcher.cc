#include "launcher/simulation_launcher.h"

#include "launcher/node_manager/node_manager.h"
#include "utils/joshua_runtime.h"

namespace launcher {

int RunSimulation(NodeManager& manager) {
  if (manager.config().robot().perceptions().single_perceptions_size() > 0) {
    const auto launched = manager.LaunchAllNodes();
    if (absl::IsCancelled(launched) && manager.ShutdownRequested()) return 0;
    joshua::ThrowIfError(launched);
  }
  const auto simulation = manager.LaunchSimulation();
  if (absl::IsCancelled(simulation) && manager.ShutdownRequested()) return 0;
  joshua::ThrowIfError(simulation);
  joshua::ThrowIfError(manager.MonitorNodes("simulation"));
  return joshua::RuntimeFailed() ? 1 : 0;
}

}  // namespace launcher
