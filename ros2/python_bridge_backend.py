from __future__ import annotations

from dataclasses import dataclass
from queue import Empty, Queue
from threading import Lock
from typing import Any, Dict, Optional, Type


@dataclass
class BridgeEnvelope:
    """Serialized ROS message accompanied with metadata for backend transport."""

    topic: str
    msg_type: Type  # rclpy message class
    payload: bytes


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


__all__ = ["BridgeEnvelope", "BridgeBackend", "LoopbackBridgeBackend"]

