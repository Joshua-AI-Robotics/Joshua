from __future__ import annotations

import asyncio
import contextlib
import logging
import os
from dataclasses import dataclass
from queue import Empty, Queue
from threading import Lock, Thread
from typing import Any, Dict, Optional, Tuple, Type

try:
    from rclpy.serialization import deserialize_message, serialize_message
    from std_msgs.msg import Float32
except ImportError:  # rclpy is only needed when using the Pybricks backend
    deserialize_message = None
    serialize_message = None
    Float32 = None


@dataclass
class BridgeEnvelope:
    """Serialized ROS message accompanied with metadata for backend transport."""

    topic: str
    msg_type: Type  # rclpy message class
    payload: bytes


@dataclass(frozen=True)
class ActuatorMapping:
    """Mapping from a ROS subscription topic to a Spike motor port."""

    topic: str
    port: str
    name: str
    encoder_data_mode: int
    limits: Tuple[float, float]


@dataclass(frozen=True)
class EncoderMapping:
    """Mapping from a ROS publisher topic to a Spike motor/sensor port."""

    topic: str
    port: str
    name: str
    publish_rate_hz: Optional[int] = None


class BridgeBackend:
    """Interface that concrete Python bridge backends must implement."""

    def start(self, metadata: Optional[Dict[str, Any]] = None) -> None:
        raise NotImplementedError

    def stop(self) -> None:
        raise NotImplementedError

    def push_outbound(self, envelope: BridgeEnvelope) -> None:
        """Called by the ROS node when a message needs to travel to the backend."""

    def poll_inbound(self) -> Optional[BridgeEnvelope]:
        """Return a single inbound envelope if available."""


class LoopbackBridgeBackend(BridgeBackend):
    """Default backend used for development and testing."""

    def __init__(self, logger=None) -> None:
        self._logger = logger
        self._queue: "Queue[BridgeEnvelope]" = Queue()
        self._running = False
        self._lock = Lock()
        self._metadata: Dict[str, Any] = {}

    def start(self, metadata: Optional[Dict[str, Any]] = None) -> None:
        with self._lock:
            if self._running:
                return
            self._metadata = metadata or {}
            self._running = True
            if self._logger:
                publisher_topics = list((self._metadata.get("publishers") or {}).keys())
                suffix = f" with publishers {publisher_topics}" if publisher_topics else ""
                self._logger.info(f"Loopback bridge backend started{suffix}")

    def stop(self) -> None:
        with self._lock:
            if not self._running:
                return
            self._running = False
            self._metadata = {}
            while not self._queue.empty():
                try:
                    self._queue.get_nowait()
                except Empty:
                    break
            if self._logger:
                self._logger.info("Loopback bridge backend stopped")

    def push_outbound(self, envelope: BridgeEnvelope) -> None:
        if not self._running:
            raise RuntimeError("Loopback backend is not running")
        # Echo messages back only if explicit publisher metadata exists to avoid
        # feeding subscriptions inadvertently.
        publishers: Dict[str, Type] = self._metadata.get("publishers") or {}
        if not publishers:
            if self._logger:
                self._logger.debug(
                    f"Loopback backend received {envelope.topic} but no publisher topics configured"
                )
            return

        for topic, msg_type in publishers.items():
            if msg_type is not envelope.msg_type:
                continue
            self._queue.put(
                BridgeEnvelope(topic=topic, msg_type=msg_type, payload=envelope.payload)
            )
            if self._logger:
                self._logger.debug(
                    f"Loopback backend mirrored payload from {envelope.topic} to {topic}"
                )
            break

    def poll_inbound(self) -> Optional[BridgeEnvelope]:
        if not self._running:
            return None
        try:
            return self._queue.get_nowait()
        except Empty:
            return None


class PybricksBridgeBackend(BridgeBackend):
    """Bridge backend that routes ROS topics to a Pybricks/Spike Prime hub."""

    def __init__(self, logger=None) -> None:
        self._logger = logger or logging.getLogger(__name__)
        self._running = False
        self._outbound: "Queue[BridgeEnvelope]" = Queue()
        self._inbound: "Queue[BridgeEnvelope]" = Queue()
        self._worker: Optional[Thread] = None
        self._lock = Lock()
        self._actuators: Dict[str, ActuatorMapping] = {}
        self._encoders: Dict[str, EncoderMapping] = {}
        self._hub_identifier: Optional[str] = None
        self._poll_interval = 0.1
        self._transport = "usb"

    def start(self, metadata: Optional[Dict[str, Any]] = None) -> None:
        metadata = metadata or {}
        with self._lock:
            if self._running:
                return

            if deserialize_message is None or serialize_message is None or Float32 is None:
                raise RuntimeError(
                    "PybricksBridgeBackend requires rclpy and std_msgs. "
                    "Ensure ROS2 is sourced before starting the bridge."
                )

            self._actuators = {
                mapping.topic: mapping for mapping in metadata.get("actuators", [])
            }
            self._encoders = {
                mapping.topic: mapping for mapping in metadata.get("encoders", [])
            }
            self._hub_identifier = metadata.get("hub") or os.environ.get("PYBRICKS_HUB")
            self._poll_interval = float(metadata.get("poll_interval", self._poll_interval))
            self._transport = (
                (metadata.get("transport") or os.environ.get("PYBRICKS_TRANSPORT", "usb"))
                .strip()
                .lower()
            )

            self._running = True
            self._worker = Thread(target=self._run_worker, daemon=True)
            self._worker.start()
            if self._logger:
                self._logger.info(
                    "Pybricks bridge backend started (transport=%s) with %d actuators, %d encoders",
                    self._transport,
                    len(self._actuators),
                    len(self._encoders),
                )

    def stop(self) -> None:
        with self._lock:
            if not self._running:
                return
            self._running = False

        if self._worker and self._worker.is_alive():
            self._worker.join(timeout=5)
        self._worker = None
        if self._logger:
            self._logger.info("Pybricks bridge backend stopped")

    def push_outbound(self, envelope: BridgeEnvelope) -> None:
        if not self._running:
            raise RuntimeError("Pybricks backend is not running")
        self._outbound.put(envelope)

    def poll_inbound(self) -> Optional[BridgeEnvelope]:
        if not self._running:
            return None
        try:
            return self._inbound.get_nowait()
        except Empty:
            return None

    # Internal implementation
    def _run_worker(self) -> None:
        """Run an asyncio loop in a background thread to talk to the hub."""
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            loop.run_until_complete(self._run_async(loop))
        except Exception as exc:
            if self._logger:
                self._logger.error("Pybricks backend worker crashed: %s", exc)
        finally:
            loop.close()

    async def _run_async(self, loop: asyncio.AbstractEventLoop) -> None:
        hub = await self._connect_hub()
        if hub is None:
            if self._logger:
                self._logger.error("Unable to start Pybricks backend: hub connection failed")
            self._running = False
            return

        poll_task = loop.create_task(self._poll_encoders(hub))
        try:
            while self._running:
                try:
                    envelope = self._outbound.get(timeout=self._poll_interval)
                except Empty:
                    await asyncio.sleep(self._poll_interval)
                    continue
                await self._handle_outbound(envelope, hub)
        finally:
            poll_task.cancel()
            with contextlib.suppress(Exception):
                await poll_task
            await hub.disconnect()

    async def _connect_hub(self) -> Optional["SpikeHubClient"]:
        try:
            client = SpikeHubClient(
                identifier=self._hub_identifier,
                logger=self._logger,
                transport=self._transport,
            )
            await client.connect()
            return client
        except ImportError as exc:
            msg = (
                "pybricksdev is required for PybricksBridgeBackend. "
                "Install it with 'pip install pybricksdev[ble]' inside your environment."
            )
            if self._logger:
                self._logger.error(msg)
            raise RuntimeError(msg) from exc
        except Exception as exc:
            if self._logger:
                self._logger.error("Failed to connect to Spike hub: %s", exc)
            return None

    async def _handle_outbound(self, envelope: BridgeEnvelope, hub: "SpikeHubClient") -> None:
        mapping = self._actuators.get(envelope.topic)
        if not mapping:
            if self._logger:
                self._logger.debug("Ignoring outbound topic %s (no actuator mapping)", envelope.topic)
            return

        if deserialize_message is None:
            raise RuntimeError("rclpy is required to deserialize outbound messages for Pybricks backend")

        try:
            ros_msg = deserialize_message(envelope.payload, envelope.msg_type)
        except Exception as exc:
            if self._logger:
                self._logger.error("Failed to deserialize outbound message for %s: %s", envelope.topic, exc)
            return

        try:
            value = self._coerce_float(ros_msg)
        except (TypeError, ValueError) as exc:
            if self._logger:
                self._logger.error("Unsupported message for %s: %s", envelope.topic, exc)
            return

        mapped_value = self._map_action_value(value, mapping)
        await hub.set_motor_target(mapping.port, mapped_value)

    async def _poll_encoders(self, hub: "SpikeHubClient") -> None:
        while self._running:
            for mapping in self._encoders.values():
                try:
                    value = await hub.read_encoder(mapping.port)
                except Exception as exc:
                    if self._logger:
                        self._logger.debug("Failed to read encoder on %s: %s", mapping.port, exc)
                    continue
                if value is None:
                    continue
                if Float32 is None or serialize_message is None:
                    raise RuntimeError(
                        "rclpy and std_msgs are required to publish encoder readings from Pybricks backend"
                    )
                msg = Float32()
                msg.data = float(value)
                payload = serialize_message(msg)
                self._inbound.put(
                    BridgeEnvelope(topic=mapping.topic, msg_type=Float32, payload=payload)
                )
            await asyncio.sleep(self._poll_interval)

    def _coerce_float(self, msg: Any) -> float:
        if hasattr(msg, "data"):
            return float(msg.data)
        raise TypeError(f"Message {msg!r} does not expose a 'data' field for actuator control")

    def _map_action_value(self, value: float, mapping: ActuatorMapping) -> float:
        low, high = mapping.limits
        span = high - low
        if mapping.encoder_data_mode == 1:  # RAW
            return value
        if mapping.encoder_data_mode == 2:  # ZERO_TO_ONE
            return low + value * span
        if mapping.encoder_data_mode == 3:  # MINUS_ONE_TO_ONE
            return low + (value + 1.0) * (span / 2.0)
        if mapping.encoder_data_mode == 4:  # RADIAN
            return low + (value + (3.141592653589793 / 2.0)) * (span / 3.141592653589793)
        return value


class SpikeHubClient:
    """Thin wrapper around pybricksdev 2.3.0 PybricksHubUSB/BLE."""

    def __init__(self, identifier: Optional[str], logger=None, transport: str = "usb") -> None:
        self._identifier = identifier
        self._logger = logger or logging.getLogger(__name__)
        self._transport = transport
        self._hub = None
        self._shadow_state: Dict[str, float] = {}

    async def connect(self) -> None:
        try:
            from pybricksdev.connections.pybricks import PybricksHubBLE, PybricksHubUSB
        except ImportError:
            raise

        hub_cls = PybricksHubUSB if self._transport == "usb" else PybricksHubBLE
        self._hub = hub_cls()
        await self._hub.connect(self._identifier)
        if self._logger:
            self._logger.info(
                "Connected to Spike hub %s via %s",
                self._identifier or "(auto)",
                self._transport,
            )

    async def disconnect(self) -> None:
        if self._hub:
            with contextlib.suppress(Exception):
                await self._hub.disconnect()
        if self._logger:
            self._logger.info("Spike hub disconnected")

    async def set_motor_target(self, port: str, value: float) -> None:
        # pybricksdev 2.3.0 does not expose direct motor control; a user program on the hub must
        # consume commands from stdout/stderr or BLE/USB characteristics. For now, we surface a
        # clear error so users know to deploy a control script.
        raise RuntimeError(
            "set_motor_target not supported directly with pybricksdev 2.3.0. "
            "Deploy a Pybricks program to handle motor commands and extend SpikeHubClient "
            "to forward commands to that program."
        )

    async def read_encoder(self, port: str) -> Optional[float]:
        # Same note as above; encoder reads require a user program on the hub.
        raise RuntimeError(
            "read_encoder not supported directly with pybricksdev 2.3.0. "
            "Deploy a Pybricks program to report encoder values and extend SpikeHubClient "
            "to retrieve them."
        )


__all__ = [
    "ActuatorMapping",
    "BridgeEnvelope",
    "BridgeBackend",
    "EncoderMapping",
    "LoopbackBridgeBackend",
    "PybricksBridgeBackend",
]
