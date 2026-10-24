#pragma once

namespace node_generator {
class NodeManager;
}

namespace launcher {

// Simulation and accompanying ROS nodes share the launcher's process owner.
int RunSimulation(node_generator::NodeManager& manager);

}  // namespace launcher
