#include "ros2/device_bus.h"

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<DeviceBus>(argc, argv, "device_bus", 2);
}
