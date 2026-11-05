#include "ros2/mcu_bus.h"

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<McuBus>(argc, argv, "mcu_bus", 2);
}
