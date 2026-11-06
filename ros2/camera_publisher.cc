#include "ros2/node_runner.h"
#include "ros2/observation_publisher.h"

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<ros2_utils::ObservationNode>(argc, argv, "camera_publisher");
}
