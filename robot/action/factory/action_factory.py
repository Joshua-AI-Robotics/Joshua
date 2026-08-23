from __future__ import annotations

from robot.action.interfaces.action_interface import ActionInterface
from robot.action.motors.drivers.pybricks_driver import PybricksMotorDriver
from robot.action.proto import action_pb2


def create_action(single_action: action_pb2.SingleAction) -> ActionInterface:
    if single_action.action_type == action_pb2.ActionType.ACTUATOR:
        actuator = single_action.actuator
        if actuator.actuator_type == action_pb2.ActuatorType.SPIKE_MOTOR:
            driver = PybricksMotorDriver(actuator)
            driver.init()
            return driver
        # MOCK_MOTOR moved to the board layer (docs/BOARD_LAYER_RFC.md §10
        # Phase 9): use motor_type: MOTOR_MOCK + board_name instead.
        raise ValueError("Invalid actuator type")

    raise ValueError("Invalid action type")
