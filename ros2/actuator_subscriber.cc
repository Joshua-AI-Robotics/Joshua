#include "ros2/action_subscriber.h"

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<ActionSubscriber>(argc, argv, "actuator_subscriber", 2);
}
