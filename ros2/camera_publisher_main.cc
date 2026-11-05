#include "ros2/camera_publisher.h"
#include "ros2/node_runner.h"

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<CameraPublisher>(argc, argv, "camera_publisher");
}
