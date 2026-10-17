"""Shared experimental event transport; no notification delivery."""

import json
import time
import uuid

from rclpy.qos import QoSProfile, ReliabilityPolicy
from std_msgs.msg import String

from ros2.proto import ros2_data_type_pb2


def model_config(config, node_id, model_type, node_type, input_type):
    matches = [m for m in config.ai.models.single_models if m.node.id == node_id]
    if len(matches) != 1:
        raise ValueError("Exactly one model configuration is required per processor")
    model = matches[0]
    if model.model_type != model_type or model.node.node_type != node_type:
        raise ValueError("Model and processor node type mismatch")
    node = model.node
    if len(node.subscriptions) != 1 or len(node.publishers) != 1:
        raise ValueError("Processor requires one input and one event output")
    if (
        node.subscriptions[0].ros2_data_type != input_type
        or node.publishers[0].ros2_data_type != ros2_data_type_pb2.STRING
        or not node.subscriptions[0].topic
        or not node.publishers[0].topic
    ):
        raise ValueError("Invalid processor topic contract")
    return model


def sensor_qos(depth):
    return QoSProfile(depth=depth, reliability=ReliabilityPolicy.BEST_EFFORT)


class EventOutput:
    def __init__(self, node, topic):
        self.node = node
        self.publisher = node.create_publisher(String, topic, 10)
        self.last_input = time.monotonic()
        self.stale = False
        self.timer = node.create_timer(1.0, self.check_stale)

    def received(self):
        self.last_input = time.monotonic()
        if self.stale:
            self.emit("input_restored")
        self.stale = False

    def emit(self, kind, observed_at_ns=None, **evidence):
        message = String()
        message.data = json.dumps(
            {
                "schema_version": 1,
                "event_id": str(uuid.uuid4()),
                "source": self.node.get_name(),
                "event_type": kind,
                "observed_at_ns": observed_at_ns or time.time_ns(),
                **evidence,
            },
            allow_nan=False,
        )
        self.publisher.publish(message)

    def check_stale(self):
        if time.monotonic() - self.last_input > 3 and not self.stale:
            self.emit("input_stale", severity="warning")
            self.node.get_logger().error("No fresh input for 3 seconds")
            self.stale = True
