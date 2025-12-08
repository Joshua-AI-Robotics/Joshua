"""Python Bridge Node for ROS2 communication.

Manual Testing Instructions:
-----------------------------
This node can be tested manually using the loopback configuration:

Prerequisites:
    source /opt/ros/humble/setup.bash

Terminal 1 - Start the bridge node:
    bazel run ros2:python_bridge -- python_bridge_node 99 config/config_preset/python_bridge_loopback.pbtxt

Terminal 2 - Publish a test command:
    ros2 topic pub pybricks/commands std_msgs/msg/String "{data: 'ping'}"
    or
    ros2 topic pub pybricks/commands std_msgs/msg/String "{data: 'hello from host'}" -r 1

Terminal 3 - Observe acknowledgements:
    ros2 topic echo pybricks/events

Optional Terminal 4 - Monitor heartbeat:
    ros2 topic echo <python_bridge_node>/status

Pybricks backend quick start (no hardware = stick with loopback):
- Export PYTHON_BRIDGE_BACKEND=pybricks and PYBRICKS_TRANSPORT=usb (or ble) to use the Pybricks backend.
- Ensure pybricksdev is installed in your ROS 2 environment; otherwise the backend will raise at startup.
- With hardware connected, publish Float32 commands on configured actuator topics and watch encoder topics/status.

Real hardware checklist:
- Install pybricksdev[usb] (or [ble]) and run Pybricks firmware on the Spike hub.
- Export PYTHON_BRIDGE_BACKEND=pybricks, PYBRICKS_TRANSPORT=usb (default), and optionally PYBRICKS_HUB to pick a hub.
- Ensure udev/permissions allow USB access; source ROS 2; run the bridge with a config that lists actuator/encoder topics mapped to Spike ports.
- Send `std_msgs/msg/Float32` commands to actuator topics and observe motion; echo encoder topics and `<node_name>/status` for feedback.

Expected Behavior:
- Commands published to pybricks/commands should echo back as events on pybricks/events
- Status topic should show periodic heartbeat updates
- This verifies the bridge node is correctly processing and responding to messages
"""
from __future__ import annotations

import functools
import os
import sys
from dataclasses import dataclass
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple, Type

# Ensure Bazel's hermetic Python can see system-installed ROS2 Python packages (rclpy, std_msgs, etc.)
sys.path.insert(0, "/opt/ros/humble/lib/python3.10/site-packages")
sys.path.insert(0, "/opt/ros/humble/local/lib/python3.10/dist-packages")
sys.path.insert(0, "/usr/lib/python3/dist-packages")

from rclpy.node import Node
from rclpy.serialization import deserialize_message, serialize_message
from std_msgs.msg import String

from config.proto import config_pb2
from robot.comm.proto import comm_pb2
from ros2 import node_runner as node_runner_py
from ros2.proto import node_pb2
from ros2.python_bridge_backend import (
    ActuatorMapping,
    BridgeEnvelope,
    EncoderMapping,
    LoopbackBridgeBackend,
    PybricksBridgeBackend,
)
from ros2.ros2_type_resolver import resolve_message_class_from_enum
from ros2.utils.qos_setting import create_qos_setting

STATUS_INTERVAL_SEC = 1.0
BACKEND_POLL_INTERVAL_SEC = 0.05


@dataclass
class BridgeBinding:
    """Metadata describing which config element is linked to the bridge."""

    scope: str
    name: str
    comm_summary: str


class PythonBridge(Node):
    """ROS2 node that forwards messages to a Python backend (pybricksdev-ready)."""

    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._node_id = node_id
        self._config = config
        self._node_matches = self._find_matching_nodes(config)
        if not self._node_matches:
            raise ValueError(f"No node with id {node_id} is defined in the config.")

        self._bindings = self._collect_bindings(config)
        qos_setting = create_qos_setting(self._node_matches[0].qos_setting)

        self._subscriptions = []
        self._subscription_types: Dict[str, Type] = {}
        # Avoid clashing with rclpy.node.Node._publishers (a list); keep our own map.
        self._publisher_handles: Dict[str, Any] = {}
        self._publisher_types: Dict[str, Type] = {}
        self._setup_subscriptions(qos_setting)
        self._setup_publishers(qos_setting)

        self._actuator_mappings = self._build_actuator_mappings(config)
        self._encoder_mappings = self._build_encoder_mappings(config)

        backend_metadata = {
            "publishers": self._publisher_types.copy(),
            "subscriptions": self._subscription_types.copy(),
            "actuators": self._actuator_mappings,
            "encoders": self._encoder_mappings,
            "hub": os.environ.get("PYBRICKS_HUB"),
            "transport": os.environ.get("PYBRICKS_TRANSPORT", "usb"),
        }
        backend_type = os.environ.get("PYTHON_BRIDGE_BACKEND", "loopback").lower()
        if backend_type in ("pybricks", "pybricksdev", "spike"):
            self._backend = PybricksBridgeBackend(logger=self.get_logger())
        else:
            self._backend = LoopbackBridgeBackend(logger=self.get_logger())
        self._backend.start(metadata=backend_metadata)
        self.get_logger().info(
            f"Python bridge backend={backend_type or 'loopback'} "
            f"(actuators={len(self._actuator_mappings)}, encoders={len(self._encoder_mappings)})"
        )

        self._messages_forwarded = 0
        self._messages_published = 0
        self._backend_errors = 0

        self._status_publisher = self.create_publisher(String, f"{node_name}/status", 10)
        self.create_timer(STATUS_INTERVAL_SEC, self._publish_status)
        self.create_timer(BACKEND_POLL_INTERVAL_SEC, self._pump_backend_queue)

        binding_summary = ", ".join(f"{b.scope}:{b.name}" for b in self._bindings) or "none"
        self.get_logger().info(
            f"Python bridge initialized for node_id={node_id} with bindings: {binding_summary}"
        )

    def _find_matching_nodes(self, config: config_pb2.Config) -> List[node_pb2.Node]:
        matches: List[node_pb2.Node] = []
        for _, node in self._iterate_nodes(config):
            if node.id == self._node_id:
                matches.append(node)
        return matches

    def _collect_bindings(self, config: config_pb2.Config) -> List[BridgeBinding]:
        bindings: List[BridgeBinding] = []
        for scope, node_owner in self._iterate_nodes(config, include_owner=True):
            node = getattr(node_owner, "node", None)
            if not node or node.id != self._node_id:
                continue
            name = getattr(node_owner, "actuator", None) or getattr(
                node_owner, "encoder", None
            ) or getattr(node_owner, "camera", None)
            if name and hasattr(name, "actuator_name"):
                name_str = name.actuator_name
            elif name and hasattr(name, "encoder_name"):
                name_str = name.encoder_name
            elif name and hasattr(name, "camera_name"):
                name_str = name.camera_name
            else:
                name_str = getattr(node_owner, "store_path", "") or scope

            comm_summary = ""
            if hasattr(node_owner, "actuator") and node_owner.HasField("actuator"):
                comm = node_owner.actuator.comm
                comm_summary = self._summarize_comm(comm)
            elif hasattr(node_owner, "encoder") and node_owner.HasField("encoder"):
                comm = node_owner.encoder.comm
                comm_summary = self._summarize_comm(comm)
            elif hasattr(node_owner, "camera") and node_owner.HasField("camera"):
                comm = node_owner.camera.comm
                comm_summary = self._summarize_comm(comm)

            bindings.append(BridgeBinding(scope=scope, name=name_str, comm_summary=comm_summary))
        return bindings

    def _build_actuator_mappings(self, config: config_pb2.Config) -> List[ActuatorMapping]:
        mappings: List[ActuatorMapping] = []
        for _, node_owner in self._iterate_nodes(config, include_owner=True):
            node = getattr(node_owner, "node", None)
            if not node or node.id != self._node_id:
                continue
            if not hasattr(node_owner, "actuator") or not node_owner.HasField("actuator"):
                continue
            actuator = node_owner.actuator
            port = self._resolve_comm_port(actuator.comm)
            limits = (actuator.operational_lower_limit, actuator.operational_upper_limit)
            for subscription in node.subscriptions:
                if not subscription.topic:
                    continue
                mappings.append(
                    ActuatorMapping(
                        topic=subscription.topic,
                        port=port,
                        name=actuator.actuator_name or subscription.topic,
                        encoder_data_mode=actuator.encoder_data_mode,
                        limits=limits,
                    )
                )
        return mappings

    def _build_encoder_mappings(self, config: config_pb2.Config) -> List[EncoderMapping]:
        mappings: List[EncoderMapping] = []
        for _, node_owner in self._iterate_nodes(config, include_owner=True):
            node = getattr(node_owner, "node", None)
            if not node or node.id != self._node_id:
                continue
            if not hasattr(node_owner, "encoder") or not node_owner.HasField("encoder"):
                continue
            encoder = node_owner.encoder
            port = self._resolve_comm_port(encoder.comm)
            for publisher in node.publishers:
                if not publisher.topic:
                    continue
                mappings.append(
                    EncoderMapping(
                        topic=publisher.topic,
                        port=port,
                        name=encoder.encoder_name or publisher.topic,
                        publish_rate_hz=publisher.publish_rate_hz or None,
                    )
                )
        return mappings

    def _resolve_comm_port(self, comm) -> str:
        if not comm:
            return ""
        try:
            if comm.comm_type == comm_pb2.SERIAL and comm.HasField("serial_config"):
                cfg = comm.serial_config
                if getattr(cfg, "port", ""):
                    return cfg.port
                if getattr(cfg, "id", 0):
                    return str(cfg.id)
        except Exception:
            pass
        return ""

    def _summarize_comm(self, comm) -> str:
        if not comm:
            return ""
        if comm.comm_type == comm_pb2.SERIAL and comm.HasField("serial_config"):
            return f"serial:{comm.serial_config.port}@{comm.serial_config.baudrate}"
        return comm_pb2.CommType.Name(comm.comm_type)

    def _iterate_nodes(
        self,
        config: config_pb2.Config,
        include_owner: bool = False,
    ) -> Iterable[Tuple[str, node_pb2.Node] | Tuple[str, Any]]:
        def maybe_yield(scope: str, owner):
            node = getattr(owner, "node", None)
            if not node:
                return
            yield (scope, owner) if include_owner else (scope, node)

        for single_action in config.robot.actions.single_actions:
            yield from maybe_yield("action", single_action)
        for single_perception in config.robot.perceptions.single_perceptions:
            yield from maybe_yield("perception", single_perception)
        for single_model in config.ai.models.single_models:
            yield from maybe_yield("ai_model", single_model)
        for single_store in config.ai.data_stores.single_data_stores:
            yield from maybe_yield("data_store", single_store)
        for single_calibration in config.calibration.single_calibrations:
            yield from maybe_yield("calibration", single_calibration)

    def _deduplicate_topics(
        self, endpoints: Sequence
    ) -> List:
        seen = set()
        unique = []
        for endpoint in endpoints:
            key = (endpoint.topic, endpoint.ros2_data_type)
            if not endpoint.topic or key in seen:
                continue
            seen.add(key)
            unique.append(endpoint)
        return unique

    def _setup_subscriptions(self, qos_setting) -> None:
        all_subscriptions = []
        for node in self._node_matches:
            all_subscriptions.extend(node.subscriptions)
        for subscription in self._deduplicate_topics(all_subscriptions):
            try:
                msg_type = resolve_message_class_from_enum(subscription.ros2_data_type)
            except (KeyError, ValueError) as exc:
                self.get_logger().error(
                    "Failed to resolve message type for subscription %s: %s",
                    subscription.topic,
                    exc,
                )
                continue
            callback = functools.partial(
                self._handle_subscription, topic=subscription.topic, msg_type=msg_type
            )
            handle = self.create_subscription(
                msg_type,
                subscription.topic,
                callback,
                qos_setting,
            )
            self._subscriptions.append(handle)
            self._subscription_types[subscription.topic] = msg_type
            self.get_logger().info(f"Bridge subscribing to {subscription.topic}")

    def _setup_publishers(self, qos_setting) -> None:
        all_publishers = []
        for node in self._node_matches:
            all_publishers.extend(node.publishers)
        for publisher in self._deduplicate_topics(all_publishers):
            try:
                msg_type = resolve_message_class_from_enum(publisher.ros2_data_type)
            except (KeyError, ValueError) as exc:
                self.get_logger().error(
                    "Failed to resolve message type for publisher %s: %s",
                    publisher.topic,
                    exc,
                )
                continue
            publisher_handle = self.create_publisher(msg_type, publisher.topic, qos_setting)
            self._publisher_handles[publisher.topic] = publisher_handle
            self._publisher_types[publisher.topic] = msg_type
            self.get_logger().info(f"Bridge publishing to {publisher.topic}")

    def _handle_subscription(self, msg, *, topic: str, msg_type: Type) -> None:
        try:
            payload = serialize_message(msg)
        except Exception as exc:
            self._backend_errors += 1
            self.get_logger().error(
                f"Failed to serialize message for topic {topic}: {exc}"
            )
            return

        envelope = BridgeEnvelope(topic=topic, msg_type=msg_type, payload=payload)
        try:
            self._backend.push_outbound(envelope)
            self._messages_forwarded += 1
        except Exception as exc:
            self._backend_errors += 1
            self.get_logger().error(
                f"Backend rejected message for topic {topic}: {exc}"
            )

    def _pump_backend_queue(self) -> None:
        while True:
            envelope = self._backend.poll_inbound()
            if envelope is None:
                break
            publisher = self._publisher_handles.get(envelope.topic)
            msg_type = self._publisher_types.get(envelope.topic)
            if not publisher or not msg_type:
                self.get_logger().warning(
                    f"Received backend payload for unknown topic {envelope.topic}"
                )
                continue
            try:
                ros_msg = deserialize_message(envelope.payload, msg_type)
            except Exception as exc:
                self._backend_errors += 1
                self.get_logger().error(
                    f"Failed to deserialize payload for {envelope.topic}: {exc}"
                )
                continue
            publisher.publish(ros_msg)
            self._messages_published += 1

    def _publish_status(self) -> None:
        status = String()
        status.data = (
            f"bridge_id={self._node_id} "
            f"forwarded={self._messages_forwarded} "
            f"published={self._messages_published} "
            f"errors={self._backend_errors} "
            f"bindings={len(self._bindings)}"
        )
        self._status_publisher.publish(status)

    def shutdown(self) -> None:
        self.get_logger().info("Shutting down python bridge")
        self._backend.stop()


def main(argv=None):
    return node_runner_py.run_node(
        PythonBridge,
        logger_name="python_bridge",
        argv=argv,
    )


if __name__ == "__main__":
    import sys

    sys.exit(main())
