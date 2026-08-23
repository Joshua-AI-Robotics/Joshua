from __future__ import annotations

import logging
from dataclasses import dataclass
from typing import Optional, Protocol

from robot.action.proto import action_packet_pb2, action_pb2
from robot.comm.proto import comm_pb2

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
    hub_id: Optional[str]
    port: str
    idle_position: float
    operational_lower_limit: float
    operational_upper_limit: float


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
        actuator: action_pb2.Actuator,
        transport: Optional[SpikeTransport] = None,
    ) -> None:
        self._actuator = actuator
        self._spec = self._parse_spec(actuator)
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

        elif action_type == "complex":
            self._handle_complex(action_packet.complex)

        elif action_type == "position":
            self._set_position(action_packet.position)

        elif action_type == "dc":
            self._set_dc(action_packet.dc)

        elif action_type == "speed":
            self._set_speed(action_packet.speed)

        elif action_type == "torque":
            self._set_torque(action_packet.torque)

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

    # -- Complex action handling (multi-field) --------------------------------

    def _handle_complex(self, complex_action: action_packet_pb2.ComplexAction) -> None:
        has_pos = complex_action.HasField("position")
        has_spd = complex_action.HasField("speed")
        has_dc = complex_action.HasField("dc")
        has_dur = complex_action.HasField("duration_ms")

        if not has_pos and not has_spd and not has_dc:
            _log.warning("Complex action has no fields set")
            return

        if has_spd:
            speed = complex_action.speed
            if speed >= 0:
                self._move_speed = speed
            else:
                _log.warning("Invalid speed value: %s", speed)

        if has_dc:
            dc = complex_action.dc
            if -100 <= dc <= 100:
                self._set_dc(dc)
            else:
                _log.warning("Invalid dc value: %s", dc)

        if has_pos and has_dur:
            _log.debug(
                "Complex with position + duration_ms "
                "(duration ignored; using run_target)"
            )

        if has_pos:
            self._set_position(complex_action.position)
        elif has_spd and has_dur:
            self._transport.run_time(
                self._spec.hub_id,
                self._spec.port,
                self._move_speed,
                complex_action.duration_ms,
            )
        elif has_spd:
            self._transport.run_speed(
                self._spec.hub_id, self._spec.port, self._move_speed
            )

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

    # -- Config parsing -------------------------------------------------------

    @staticmethod
    def _parse_spec(actuator: action_pb2.Actuator) -> SpikeMotorSpec:
        if actuator.actuator_type != action_pb2.ActuatorType.SPIKE_MOTOR:
            raise ValueError("Actuator type must be SPIKE_MOTOR")

        if actuator.comm.comm_type != comm_pb2.BLE:
            raise ValueError("SPIKE_MOTOR requires comm_type BLE")

        config = actuator.spike_motor_config
        if not config.port:
            raise ValueError("SpikeMotorConfig.port must be set (e.g., 'A')")

        hub_id = config.hub_id or None
        return SpikeMotorSpec(
            hub_id=hub_id,
            port=config.port,
            idle_position=config.idle_position,
            operational_lower_limit=actuator.operational_lower_limit,
            operational_upper_limit=actuator.operational_upper_limit,
        )
