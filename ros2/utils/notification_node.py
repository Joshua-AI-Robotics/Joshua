"""Forward Joshua inference and media events to Home Assistant."""

import json
import queue
import time

from rclpy.node import Node
from std_msgs.msg import String

from ros2.node_runner import run_node
from ros2.proto import node_pb2, ros2_data_type_pb2
from ros2.utils.notification import EventPolicy, HomeAssistantSink, validate_options


class NotificationNode(Node):
    def __init__(self, name, node_id, config):
        super().__init__(name)
        matches = [n for n in config.utilities.notifications if n.node.id == node_id]
        if len(matches) != 1 or matches[0].node.node_type != node_pb2.NOTIFICATION_NODE:
            raise ValueError("Exactly one NOTIFICATION_NODE configuration is required")
        options = matches[0]
        validate_options(options)
        n = options.node
        topics = [s.topic for s in n.subscriptions]
        if (
            not topics
            or len(set(topics)) != len(topics)
            or not all(topics)
            or any(
                s.ros2_data_type != ros2_data_type_pb2.STRING for s in n.subscriptions
            )
            or len(n.publishers) != 1
            or not n.publishers[0].topic
            or n.publishers[0].topic in topics
            or n.publishers[0].ros2_data_type != ros2_data_type_pb2.STRING
        ):
            raise ValueError(
                "Notification inputs and distinct status output must be STRING"
            )
        self.events = queue.Queue(maxsize=64)
        self.output = self.create_publisher(String, n.publishers[0].topic, 10)
        self.policy = EventPolicy(options, topics)
        self.sink = HomeAssistantSink(options, self.report)
        self.subscriptions_ = [
            self.create_subscription(
                String, topic, lambda msg, t=topic: self.receive(t, msg), 20
            )
            for topic in topics
        ]
        self.last_status = 0
        self.last_error = 0
        self.timer = self.create_timer(1, self.tick)
        self.sink.start()
        self.get_logger().info(
            "Home Assistant bridge ready; phone alerts are controlled in its dashboard"
        )

    def report(self, state, detail):
        try:
            self.events.put_nowait((state, detail))
        except queue.Full:
            pass

    def receive(self, topic, message):
        try:
            payload = self.policy.receive(topic, message.data)
            if payload and payload["notify"]:
                self.sink.submit(payload)
        except (ValueError, TypeError, KeyError):
            if time.monotonic() - self.last_error > 5:
                self.last_error = time.monotonic()
                self.get_logger().warning("Discarded invalid or stale inference event")

    def tick(self):
        for payload in self.policy.tick():
            self.sink.submit(payload)
        if time.monotonic() - self.last_status >= 5:
            self.last_status = time.monotonic()
            self.sink.submit(self.policy.envelope())
        while not self.events.empty():
            state, detail = self.events.get_nowait()
            self.output.publish(
                String(
                    data=json.dumps(
                        {
                            "schema_version": 1,
                            "event_type": state,
                            "summary": detail,
                            "observed_at_ns": time.time_ns(),
                            "source": self.get_name(),
                        }
                    )
                )
            )
            if state in ("delivery_error", "queue_full"):
                self.get_logger().warning(f"{state}: {detail}")

    def shutdown(self):
        self.sink.close()


if __name__ == "__main__":
    raise SystemExit(run_node(NotificationNode, "notification_node"))
