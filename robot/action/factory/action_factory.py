from __future__ import annotations

from robot.action.interfaces.action_interface import ActionInterface
from robot.action.motors.drivers.mock_driver import MockDriver
from robot.action.proto import action_pb2


def create_action(single_action: action_pb2.SingleAction) -> ActionInterface:
    if single_action.action_type != action_pb2.ActionType.ACTUATOR:
        raise ValueError("Invalid action type")

    actuator = single_action.actuator
    driver = MockDriver(actuator)
    driver.init()
    return driver

    raise ValueError("Invalid actuator type")
