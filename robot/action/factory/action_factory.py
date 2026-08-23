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
        raise ValueError(
            f"actuator_type {actuator.actuator_type} is not supported by the Python "
            "factory; MOCK_MOTOR moved to the C++ board layer — use motor_type: "
            "MOTOR_MOCK + board_name + channel (docs/BOARD_LAYER_RFC.md §10 Phase 9)"
        )

    raise ValueError("Invalid action type")
