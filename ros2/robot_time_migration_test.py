import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

from robot.action.proto import action_packet_pb2
from ros2.image_converter import numpy_to_imgmsg
from ros2.trajectory_publisher import TrajectoryPublisher
from ros2.utils.robot_time_conversion import from_ros_timestamp, to_ros_timestamp


class RobotTimeMigrationTest(unittest.TestCase):
    def test_ros_conversion_is_exact_and_checks_range(self):
        minimum = -(2**31) * 1_000_000_000
        maximum = (2**31 - 1) * 1_000_000_000 + 999_999_999
        for ns in (0, -1, -500_000_000, 1_700_000_000_123_456_789, minimum, maximum):
            with self.subTest(ns=ns):
                self.assertEqual(from_ros_timestamp(to_ros_timestamp(ns)), ns)
        for ns in (minimum - 1, maximum + 1):
            with self.assertRaises(ValueError):
                to_ros_timestamp(ns)

    def test_trajectory_preserves_explicit_zero_negative_and_exact_source_time(self):
        packet = action_packet_pb2.ActionPacket()
        packet.joint.joint_name = "elbow"
        packet.joint.position = 1.0
        packet.joint.position_encoding = action_packet_pb2.JointCommand.POSITION_SI
        with patch("ros2.trajectory_publisher.robot_time_ns", return_value=123) as now:
            for ns in (0, -500_000_000, 1_700_000_000_123_456_789):
                packet.timestamp_ns = ns
                message = TrajectoryPublisher._joint_state_message(None, packet)
                self.assertEqual(from_ros_timestamp(message.header.stamp), ns)
            now.assert_not_called()
            packet.ClearField("timestamp_ns")
            message = TrajectoryPublisher._joint_state_message(None, packet)
            self.assertEqual(from_ros_timestamp(message.header.stamp), 123)
            now.assert_called_once()

    def test_image_conversion_retains_source_time_and_stamps_new_images(self):
        image = np.zeros((2, 3, 3), dtype=np.uint8)
        with patch("ros2.image_converter.robot_time_ns", return_value=123) as now:
            for ns in (0, -1, 1_700_000_000_123_456_789):
                message = numpy_to_imgmsg(image, timestamp_ns=ns)
                self.assertEqual(from_ros_timestamp(message.header.stamp), ns)
                self.assertEqual((message.height, message.width), (2, 3))
            now.assert_not_called()
            message = numpy_to_imgmsg(image)
            self.assertEqual(from_ros_timestamp(message.header.stamp), 123)

    def test_python_runner_initializes_clock_before_node_construction(self):
        self.run_runner(
            "source: MONOTONIC",
            """
import time
from rclpy.node import Node
from utils.robot_time import robot_time_ns
class TestNode(Node):
    def __init__(self, *args):
        super().__init__('robot_time_test')
        before = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        stamp = robot_time_ns()
        after = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        assert before <= stamp <= after
        self.timer = self.create_timer(0.001, self.finish)
    def finish(self):
        assert robot_time_ns() > 0
        raise KeyboardInterrupt
""",
            expected=0,
        )

    def test_python_runner_clock_failure_prevents_construction(self):
        self.run_runner(
            "require_ptp: true ptp_utc_offset_seconds: 0 "
            "ptp_device: '/nonexistent-joshua-test/ptp'",
            """
class TestNode:
    def __init__(self, *args):
        raise AssertionError('Node constructed before clock setup')
""",
            expected=1,
        )

    def run_runner(self, clock_config, node_code, expected):
        with tempfile.TemporaryDirectory() as temp:
            path = os.path.join(temp, "config.pbtxt")
            with open(path, "w") as file:
                file.write(f"general {{ robot_clock {{ {clock_config} }} }}")
            code = (
                "from ros2.node_runner import run_node\nimport rclpy\n"
                + node_code
                + "\nraise SystemExit(run_node(TestNode, 'test', "
                + f"['test', 'test', '1', {path!r}]))"
            )
            env = dict(os.environ, ROS_LOG_DIR=temp)
            result = subprocess.run(
                [sys.executable, "-c", code],
                env=env,
                capture_output=True,
                text=True,
                timeout=20,
            )
            self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
            self.assertNotIn("Node constructed before clock setup", result.stderr)


if __name__ == "__main__":
    unittest.main()
