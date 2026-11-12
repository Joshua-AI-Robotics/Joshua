#pragma once

namespace launcher {

class NodeManager;

// Simulation and accompanying ROS nodes share the launcher's process owner.
int RunSimulation(NodeManager& manager);

}  // namespace launcher
