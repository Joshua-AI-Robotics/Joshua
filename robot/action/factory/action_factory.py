from __future__ import annotations

from robot.action.interfaces.action_interface import ActionInterface
from robot.action.motors.drivers.mock_driver import MockDriver
from robot.action.proto import action_pb2


def create_action(single_action: action_pb2.SingleAction) -> ActionInterface:
    if single_action.action_type == action_pb2.ActionType.ACTUATOR:
        actuator = single_action.actuator
        if actuator.actuator_type == action_pb2.ActuatorType.MOCK_MOTOR:
            driver = MockDriver(actuator)
            driver.init()
            return driver
        raise ValueError("Invalid actuator type")

    raise ValueError("Invalid action type")
