from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Protocol

from robot.action.interfaces.actuator_interface import ActuatorInterface
from robot.action.proto import action_packet_pb2, action_pb2
from robot.comm.proto import comm_pb2


class SpikeTransport(Protocol):
    def connect(self, hub_id: Optional[str]) -> None:
        raise NotImplementedError

    def disconnect(self, hub_id: Optional[str]) -> None:
        raise NotImplementedError

    def set_motor_angle(self, hub_id: Optional[str], port: str, angle: float) -> None:
        raise NotImplementedError


@dataclass(frozen=True)
class SpikeMotorSpec:
    hub_id: Optional[str]
    port: str
    idle_position: float


class PybricksMotorDriver(ActuatorInterface):
    def __init__(
        self,
        actuator: action_pb2.Actuator,
        transport: Optional[SpikeTransport] = None,
    ) -> None:
        self._actuator = actuator
        self._spec = self._parse_spec(actuator)
        self._owns_transport = False
        if transport is None:
            from robot.comm.pybricks_ble_transport import PybricksBleTransport

            transport = PybricksBleTransport.get_shared(self._spec.hub_id)
            self._owns_transport = True
        self._transport = transport

    def init(self) -> None:
        self._transport.connect(self._spec.hub_id)

    def get_id(self) -> str:
        hub = self._spec.hub_id or "default"
        return f"spike_motor:{hub}:{self._spec.port}"

    def set_action(self, action_packet: action_packet_pb2.ActionPacket) -> None:
        angle = float(action_packet.position)
        self._transport.set_motor_angle(self._spec.hub_id, self._spec.port, angle)

    def teardown(self) -> None:
        try:
            self._transport.set_motor_angle(
                self._spec.hub_id, self._spec.port, self._spec.idle_position
            )
        except Exception:
            pass

        if self._owns_transport:
            from robot.comm.pybricks_ble_transport import PybricksBleTransport

            PybricksBleTransport.release_shared(self._spec.hub_id)
        else:
            self._transport.disconnect(self._spec.hub_id)

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
        )
