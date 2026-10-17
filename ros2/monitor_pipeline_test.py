"""Two real inference subprocesses driven exclusively by synthetic ROS data."""

import json
import os
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

import rclpy
from config.proto.config_pb2 import Config
from google.protobuf import text_format
from robot.perception.proto.perception_packet_pb2 import PerceptionPacket
from sensor_msgs.msg import Image
from std_msgs.msg import String, UInt8MultiArray

from ros2.monitor_common import sensor_qos


class PipelineTest(unittest.TestCase):
    def test_independent_processors_and_stale_input(self):
        config = Config()
        text_format.Parse(
            Path("config/config_preset/baby_monitor/pi5_usb.pbtxt").read_text(), config
        )
        # Only inference processes are launched. No sensor config or device access.
        config.ClearField("robot")
        os.environ["ROS_DOMAIN_ID"] = str(70 + os.getpid() % 100)
        os.environ["ROS_LOCALHOST_ONLY"] = "1"
        processes = []
        observed = []
        with tempfile.TemporaryDirectory() as temporary:
            os.environ["ROS_LOG_DIR"] = str(Path(temporary) / "ros_logs")
            path = Path(temporary) / "synthetic.pbtxt"
            path.write_text(text_format.MessageToString(config))
            rclpy.init(args=[])
            node = rclpy.create_node("synthetic_monitor_test")
            audio_pub = node.create_publisher(
                UInt8MultiArray, "/audio/pcm", sensor_qos(10)
            )
            image_pub = node.create_publisher(Image, "/camera/image", sensor_qos(1))
            subscriptions = [
                node.create_subscription(
                    String, topic, lambda msg: observed.append(json.loads(msg.data)), 10
                )
                for topic in ("/audio/events", "/video/events")
            ]
            try:
                for binary, node_id in (
                    ("yamnet_node", 3),
                    ("video_inference_node", 4),
                ):
                    processes.append(
                        subprocess.Popen(
                            [
                                str(Path("ros2", binary).resolve()),
                                binary,
                                str(node_id),
                                str(path),
                            ],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True,
                        )
                    )
                deadline = time.monotonic() + 20
                sequence = 0
                while time.monotonic() < deadline:
                    now = time.time_ns()
                    packet = PerceptionPacket(
                        perception_id="synthetic",
                        timestamp_ns=now,
                        sequence_id=sequence,
                    )
                    packet.audio.sample_rate_hz = 16000
                    packet.audio.channels = 1
                    packet.audio.frames = 320
                    packet.audio.encoding = "s16le"
                    packet.audio.data = b"\x00" * 640
                    audio_pub.publish(UInt8MultiArray(data=packet.SerializeToString()))
                    if sequence % 5 == 0:
                        message = Image(height=48, width=64, encoding="rgb8", step=192)
                        message.header.stamp.sec, message.header.stamp.nanosec = divmod(
                            now, 1_000_000_000
                        )
                        message.data = b"\x00" * (48 * 192)
                        image_pub.publish(message)
                    sequence += 1
                    rclpy.spin_once(node, timeout_sec=0)
                    time.sleep(0.02)
                    kinds = {event["event_type"] for event in observed}
                    if {"audio_classification", "video_detection"} <= kinds:
                        break
                self.assertTrue(
                    all(process.poll() is None for process in processes),
                    "Inference process exited",
                )
                self.assertTrue(
                    {"audio_classification", "video_detection"} <= kinds, observed
                )
                # Stop publishing; both live processes must report input loss.
                deadline = time.monotonic() + 6
                while time.monotonic() < deadline:
                    rclpy.spin_once(node, timeout_sec=0.1)
                stale_sources = {
                    event["source"]
                    for event in observed
                    if event["event_type"] == "input_stale"
                }
                self.assertEqual(stale_sources, {"yamnet_node", "video_inference_node"})
                self.assertFalse(
                    [
                        event
                        for event in observed
                        if event["event_type"] == "inference_error"
                    ]
                )
                self.assertEqual(len(subscriptions), 2)
            finally:
                for process in processes:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        output, _ = process.communicate(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        output, _ = process.communicate()
                    print(output)
                node.destroy_node()
                rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
