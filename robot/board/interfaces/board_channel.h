#pragma once

#include "robot/board/interfaces/actuator_channel.h"
#include "robot/board/interfaces/sensor_channel.h"

namespace robot::board {

// One slot on one board that both commands and measures — a smart servo, a
// motor with an encoder, a PDO joint. How the command reaches the
// controller (serial frame, UDP frame, EtherCAT PDO field, servo bus
// register write) is the board's problem.
//
// This is the union of the two role interfaces, not a third interface:
// consumers take the half they need. ActionFactory hands a motor driver the
// whole channel; PerceptionFactory narrows to SensorChannel, so a sensor
// holds no method that can move a motor (docs/BOARD_LAYER_RFC.md §5.3).
// A channel that only drives or only senses implements just its own half.
class BoardChannel : public ActuatorChannel, public SensorChannel {};

}  // namespace robot::board
