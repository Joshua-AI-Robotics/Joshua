#include "ros2/action_subscriber.h"

JOSHUA_MAIN(argc, argv) {
  return ros2_utils::RunNode<ActionSubscriber>(argc, argv, "actuator_subscriber", 2);
}
