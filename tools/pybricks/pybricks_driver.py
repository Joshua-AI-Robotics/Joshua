from __future__ import annotations

import logging
import math
from dataclasses import dataclass
from typing import Optional, Protocol

from robot.action.proto import action_packet_pb2

_log = logging.getLogger(__name__)

_DEFAULT_SPEED = 500


class SpikeTransport(Protocol):
    def connect(self, hub_id: Optional[str]) -> None: ...

    def disconnect(self, hub_id: Optional[str]) -> None: ...

    def set_motor_angle(
        self, hub_id: Optional[str], port: str, angle: float
    ) -> None: ...

    def run_target(
        self, hub_id: Optional[str], port: str, speed: float, angle: float
    ) -> None: ...

    def run_speed(self, hub_id: Optional[str], port: str, speed: float) -> None: ...

    def set_dc(self, hub_id: Optional[str], port: str, duty: float) -> None: ...

    def stop(self, hub_id: Optional[str], port: str) -> None: ...

    def brake(self, hub_id: Optional[str], port: str) -> None: ...

    def hold(self, hub_id: Optional[str], port: str) -> None: ...

    def reset_angle(
        self, hub_id: Optional[str], port: str, angle: float = 0
    ) -> None: ...

    def run_time(
        self, hub_id: Optional[str], port: str, speed: float, time_ms: float
    ) -> None: ...

    def run_angle(
        self, hub_id: Optional[str], port: str, speed: float, angle: float
    ) -> None: ...


@dataclass(frozen=True)
class SpikeMotorSpec:
    """How to reach one motor on one hub.

    This is the tool's own config surface. It deliberately does not use
    robot.action.Actuator: SPIKE_MOTOR, SpikeMotorConfig, and CommType.BLE
    were removed from the robot protos when Spike support was dropped
    (docs/BOARD_LAYER_RFC.md §10 Phase 9), so nothing Spike-shaped remains
    outside this directory.
    """

    port: str
    hub_id: Optional[str] = None
    idle_position: float = 0.0
    operational_lower_limit: float = 0.0
    operational_upper_limit: float = 0.0

    def __post_init__(self) -> None:
        if not self.port:
            raise ValueError("SpikeMotorSpec.port must be set (e.g., 'A')")


class PybricksMotorDriver:
    """Host-side Pybricks/SPIKE motor driver for bench bring-up.

    Not on the runtime path, and the only way to drive a SPIKE hub. This lived
    under robot/ until the Python robot layer was removed
    (docs/BOARD_LAYER_RFC.md §10 Phase 9); the SPIKE_HUB_BLE board type and
    MOTOR_SPIKE went too, so the launcher cannot reach a hub from a preset.
    It no longer implements ActuatorInterface — that ABC was deleted with the
    rest of the Python robot layer — but keeps the same
    init/get_id/set_action/teardown shape.
    """

    def __init__(
        self,
        spec: SpikeMotorSpec,
        transport: Optional[SpikeTransport] = None,
    ) -> None:
        self._spec = spec
        self._move_speed: float = _DEFAULT_SPEED
        self._owns_transport = False
        if transport is None:
            from tools.pybricks.pybricks_ble_transport import PybricksBleTransport

            transport = PybricksBleTransport.get_shared(self._spec.hub_id)
            self._owns_transport = True
        self._transport = transport

    def init(self) -> None:
        self._transport.connect(self._spec.hub_id)

    def get_id(self) -> str:
        hub = self._spec.hub_id or "default"
        return f"spike_motor:{hub}:{self._spec.port}"

    # -- ActionPacket dispatch ------------------------------------------------

    def set_action(self, action_packet: action_packet_pb2.ActionPacket) -> None:
        action_type = action_packet.WhichOneof("action_type")
        _log.debug("ActionPacket [%s] type=%s", action_packet.action_id, action_type)

        if action_type == "preset":
            self._handle_preset(action_packet.preset)

        elif action_type == "joint":
            self._handle_joint(action_packet.joint)

        else:
            _log.warning(
                "No action type set in ActionPacket [%s]",
                action_packet.action_id,
            )

    # -- Preset handling ------------------------------------------------------

    def _handle_preset(self, preset: int) -> None:
        hub, port = self._spec.hub_id, self._spec.port
        preset_enum = action_packet_pb2.PresetCommand

        if preset == preset_enum.PRESET_MIDDLE_POSITION:
            middle = (
                self._spec.operational_lower_limit + self._spec.operational_upper_limit
            ) / 2.0
            self._transport.run_target(hub, port, self._move_speed, middle)

        elif preset == preset_enum.PRESET_IDLE_POSITION:
            self._transport.run_target(
                hub, port, self._move_speed, self._spec.idle_position
            )

        elif preset == preset_enum.PRESET_TEARDOWN:
            self._transport.run_target(
                hub, port, _DEFAULT_SPEED, self._spec.idle_position
            )

        elif preset == preset_enum.PRESET_ENABLE_TORQUE:
            self._transport.hold(hub, port)

        elif preset == preset_enum.PRESET_DISABLE_TORQUE:
            self._transport.stop(hub, port)

        elif preset == preset_enum.PRESET_RESET_ENCODER:
            self._transport.reset_angle(hub, port, 0)

        else:
            _log.warning("Unknown preset command: %s", preset)

    def _handle_joint(self, command: action_packet_pb2.JointCommand) -> None:
        if command.joint_name != self._spec.port:
            raise ValueError("Joint name must match the Pybricks port")
        if command.position_encoding != action_packet_pb2.JointCommand.POSITION_NATIVE:
            raise ValueError("Pybricks tool requires native position encoding")
        if command.units != action_packet_pb2.JointCommand.NATIVE:
            raise ValueError(
                "Pybricks tool supports native degrees, deg/s and duty percent only"
            )
        fields = [f for f in ("position", "velocity", "effort") if command.HasField(f)]
        if not fields or any(not math.isfinite(getattr(command, f)) for f in fields):
            raise ValueError("Joint command requires finite values")
        if command.HasField("effort"):
            if len(fields) != 1 or not -100 <= command.effort <= 100:
                raise ValueError("Duty cycle must be alone and in [-100, 100]")
            self._set_dc(command.effort)
            return
        if command.HasField("position"):
            if (
                not self._spec.operational_lower_limit
                <= command.position
                <= self._spec.operational_upper_limit
            ):
                raise ValueError("Position outside operational limits")
            if command.HasField("velocity"):
                if command.velocity < 0:
                    raise ValueError("Position move speed must be nonnegative")
                self._move_speed = command.velocity
            self._set_position(command.position)
        else:
            self._set_speed(command.velocity)

    # -- Primitive motor operations -------------------------------------------

    def _set_position(self, angle: float) -> None:
        lo = self._spec.operational_lower_limit
        hi = self._spec.operational_upper_limit
        if lo <= angle <= hi:
            self._transport.run_target(
                self._spec.hub_id, self._spec.port, self._move_speed, angle
            )
        else:
            _log.warning(
                "Position %s outside operational limits [%s, %s]", angle, lo, hi
            )

    def _set_speed(self, speed: float) -> None:
        self._move_speed = speed
        self._transport.run_speed(self._spec.hub_id, self._spec.port, speed)

    def _set_dc(self, duty: float) -> None:
        """Sets Pybricks dc() duty cycle (-100 to 100)."""
        self._transport.set_dc(self._spec.hub_id, self._spec.port, duty)

    def _set_torque(self, value: float) -> None:
        """Torque enable/disable: >0 holds position, 0 coasts."""
        hub, port = self._spec.hub_id, self._spec.port
        if value > 0:
            self._transport.hold(hub, port)
        else:
            self._transport.stop(hub, port)

    # -- Lifecycle ------------------------------------------------------------

    def teardown(self) -> None:
        try:
            self._transport.reset_angle(self._spec.hub_id, self._spec.port, 0)
            self._transport.run_target(
                self._spec.hub_id,
                self._spec.port,
                _DEFAULT_SPEED,
                self._spec.idle_position,
            )
        except Exception:
            pass

        if self._owns_transport:
            from tools.pybricks.pybricks_ble_transport import PybricksBleTransport

            PybricksBleTransport.release_shared(self._spec.hub_id)
        else:
            self._transport.disconnect(self._spec.hub_id)
