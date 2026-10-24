#include "utils/robot_time.h"

// A separate translation unit must resolve the same process-global clock.
double RobotTimeFromHelper() {
  return joshua::RobotTime();
}
