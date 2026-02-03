from __future__ import annotations

from robot.action.interfaces.action_interface import ActionInterface


class ActuatorInterface(ActionInterface):
    """Abstract actuator interface."""

    def set_speed(self, value: float) -> None:
        raise NotImplementedError

    def set_position(self, angle: float) -> None:
        raise NotImplementedError

    def set_torque(self, torque: float) -> None:
        raise NotImplementedError

    def set_middle_position(self) -> None:
        # Optional override
        return None

    def set_idle_position(self) -> None:
        # Optional override
        return None
