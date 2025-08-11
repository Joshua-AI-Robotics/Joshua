import sys
import threading
import random
from typing import List

import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32
from sensor_msgs.msg import Image

# Protobuf generated modules
from config.proto import config_pb2
from config.proto import ai_pb2

from ros2 import node_runner as node_runner_py


class MockInferencePy(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)

        if config.general.operation_mode != config_pb2.General.OperationMode.MODE_MOCK_INFERENCE_PY:
            self.get_logger().error("Mock inference node is only supported in mock inference mode.")
            return

        # Initialize publishers
        self.action_publishers: List[rclpy.publisher.Publisher] = []
        for i in range(len(config.ai.publish_topics)):
            topic = config.ai.publish_topics[i]
            self.action_publishers.append(self.create_publisher(Float32, topic, 10))

        # Initialize state tracking
        self.latest_states: List[float] = [0.0 for _ in range(len(config.ai.subscribe_topics))]
        self.received: List[bool] = [False for _ in range(len(config.ai.subscribe_topics))]

        # Random noise generator bounds
        self.noise_low: float = -0.02
        self.noise_high: float = 0.02

        # Subscriptions
        self.state_subscriptions = []
        for i in range(len(config.ai.subscribe_topics)):
            topic = config.ai.subscribe_topics[i]
            # Note: message type is currently hardcoded to Image
            self.state_subscriptions.append(
                self.create_subscription(Image, topic, self._make_image_cb(i), 10)
            )
            self.get_logger().info(f"Subscribed to image state topic: {topic}")

        self._mutex = threading.Lock()
        self.get_logger().info(
            f"Mock inference node started ({len(config.ai.publish_topics)} actions, {len(config.ai.subscribe_topics)} states)."
        )

    def _make_image_cb(self, state_index: int):
        def _cb(msg: Image):
            del msg  # unused
            with self._mutex:
                if state_index >= len(self.latest_states):
                    self.get_logger().warn(f"Received state index out of range: {state_index}")
                    return
                # Generate a random state value
                self.latest_states[state_index] = random.uniform(self.noise_low, self.noise_high)
                self.received[state_index] = True

                # If we have a fresh message from all topics, publish once
                if all(self.received):
                    self._publish_actions_locked()
                    self.received = [False for _ in self.received]
        return _cb

    def _publish_actions_locked(self) -> None:
        if not self.latest_states:
            return
        aggregated_state = sum(self.latest_states) / max(1, len(self.latest_states))
        for publisher in self.action_publishers:
            msg = Float32()
            msg.data = aggregated_state + random.uniform(self.noise_low, self.noise_high)
            publisher.publish(msg)


def main(argv=None):
    return node_runner_py.run_node(MockInferencePy, logger_name="mock_inference_py", argv=argv)


if __name__ == "__main__":
    sys.exit(main()) 