"""Exercise real ROS event subscription and bridge shutdown without hardware."""

import json
import os
import tempfile
import time
import unittest
from unittest.mock import patch

import rclpy
from config.proto.config_pb2 import Config
from google.protobuf import text_format
from rclpy.executors import SingleThreadedExecutor
from rclpy.node import Node
from std_msgs.msg import String

from ros2.utils.notification_node import NotificationNode


class NodeTest(unittest.TestCase):
    def test_ros_delivery_and_shutdown(self):
        config = text_format.Parse(
            """utilities { notifications {
          node { id: 6 node_type: NOTIFICATION_NODE
            subscriptions { topic: "/test/audio/events" ros2_data_type: STRING }
            publishers { topic: "/test/notification/events" ros2_data_type: STRING }
          }
          home_assistant_url: "http://127.0.0.1:8123"
          webhook_file_env: "JOSHUA_TEST_WEBHOOK_FILE"
          request_timeout_sec: 1 max_event_age_sec: 30 cooldown_sec: 60
          movement_persistence_sec: 2 input_timeout_sec: 15
        }}""",
            Config(),
        )
        log_dir = tempfile.TemporaryDirectory()
        self.addCleanup(log_dir.cleanup)
        env = patch.dict(
            os.environ,
            {
                "ROS_LOG_DIR": log_dir.name,
                "ROS_DOMAIN_ID": "187",
                "ROS_LOCALHOST_ONLY": "1",
            },
        )
        env.start()
        self.addCleanup(env.stop)
        rclpy.init()
        sent = []
        with patch(
            "ros2.utils.notification.HomeAssistantSink.send",
            side_effect=lambda p: sent.append(p),
        ):
            bridge = NotificationNode("notification_test", 6, config)
            producer = Node("notification_test_producer")
            pub = producer.create_publisher(String, "/test/audio/events", 10)
            executor = SingleThreadedExecutor()
            executor.add_node(bridge)
            executor.add_node(producer)
            try:
                deadline = time.monotonic() + 5
                while pub.get_subscription_count() == 0 and time.monotonic() < deadline:
                    executor.spin_once(timeout_sec=0.05)
                self.assertGreater(pub.get_subscription_count(), 0)
                pub.publish(
                    String(
                        data=json.dumps(
                            dict(
                                schema_version=1,
                                event_id="ros-cry",
                                event_type="cry_started",
                                observed_at_ns=time.time_ns(),
                            )
                        )
                    )
                )
                while (
                    not any(p["notify"] for p in sent) and time.monotonic() < deadline
                ):
                    executor.spin_once(timeout_sec=0.05)
                self.assertTrue(any(p["notify"] and p["crying"] for p in sent))
            finally:
                bridge.shutdown()
                bridge.destroy_node()
                producer.destroy_node()
                executor.shutdown()
                rclpy.shutdown()
            self.assertFalse(bridge.sink.thread.is_alive())


if __name__ == "__main__":
    unittest.main()
