#include "ros2/device_bus.h"

JOSHUA_MAIN(argc, argv) {
  return ros2_utils::RunNode<DeviceBus>(argc, argv, "device_bus", 2);
}
