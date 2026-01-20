from __future__ import annotations

from robot.action.interfaces.actuator_interface import ActuatorInterface
from robot.action.proto import action_packet_pb2, action_pb2


class MockDriver(ActuatorInterface):
    """Mock actuator driver for pipeline validation."""

    def __init__(self, actuator_config: action_pb2.Actuator) -> None:
        self._config = actuator_config
        self._initialized = False

    def init(self) -> None:
        self._initialized = True

    def get_id(self) -> str:
        return f"mock_motor_{self._config.mock_motor_config.motor_id}"

    def set_action(self, action_packet: action_packet_pb2.ActionPacket) -> None:
        # Accept all actions without hardware side effects.
        if not self._initialized:
            raise RuntimeError("MockDriver not initialized")

    def teardown(self) -> None:
        self._initialized = False

    def set_speed(self, value: float) -> None:
        if not self._initialized:
            raise RuntimeError("MockDriver not initialized")

    def set_position(self, angle: float) -> None:
        if not self._initialized:
            raise RuntimeError("MockDriver not initialized")

    def set_torque(self, torque: float) -> None:
        if not self._initialized:
            raise RuntimeError("MockDriver not initialized")
