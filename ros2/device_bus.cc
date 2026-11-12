#include "ros2/device_bus.h"
#include "ros2/node_runner.h"

JOSHUA_MAIN(argc, argv) {
  return ros2_utils::RunNode<DeviceBus>(argc, argv, "device_bus", 2);
}
