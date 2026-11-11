#include "ros2/mcu_bus.h"

JOSHUA_MAIN(argc, argv) {
  return ros2_utils::RunNode<McuBus>(argc, argv, "mcu_bus", 2);
}
