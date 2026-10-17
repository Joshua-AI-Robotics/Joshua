"""Check the ROS wire boundary without devices or network services."""

import os
import tempfile
import unittest
from unittest.mock import patch

import rclpy
from google.protobuf import text_format
from robot.perception.proto.perception_packet_pb2 import PerceptionPacket
from sensor_msgs.msg import Image
from std_msgs.msg import UInt8MultiArray

from config.proto import config_pb2
from ros2.utils.media_stream_node import MediaStreamNode, audio_sample, image_sample


class WireTest(unittest.TestCase):
    def test_image_stride_and_timestamp(self):
        message = Image(width=1, height=2, step=5, encoding="bgr8")
        message.data = b"abcXXdefXX"
        message.header.stamp.sec = 12
        message.header.stamp.nanosec = 34
        sample = image_sample(message, 15)
        self.assertEqual(sample.data, b"abc\x00def\x00")
        self.assertEqual(sample.stamp_ns, 12_000_000_034)
        self.assertIn("format=BGR", sample.caps)
        message.step = 2
        with self.assertRaises(ValueError):
            image_sample(message, 15)

    def test_pcm_format_and_duration(self):
        packet = PerceptionPacket(timestamp_ns=123)
        packet.audio.encoding = "s16le"
        packet.audio.channels = 2
        packet.audio.sample_rate_hz = 16000
        packet.audio.frames = 320
        packet.audio.data = b"\x01\x00" * 640
        packet.audio.discontinuity = True
        sample = audio_sample(UInt8MultiArray(data=packet.SerializeToString()))
        self.assertEqual(sample.duration_ns, 20_000_000)
        self.assertEqual(sample.stamp_ns, 123)
        self.assertTrue(sample.discontinuity)
        self.assertIn("channels=2", sample.caps)
        packet.audio.data = b"\x00"
        with self.assertRaises(ValueError):
            audio_sample(UInt8MultiArray(data=packet.SerializeToString()))

    def test_node_selects_typed_inputs_and_cleans_up(self):
        config = config_pb2.Config()
        text_format.Parse(
            """utilities { media_streams {
          node {
            id: 5 node_type: MEDIA_STREAM_NODE
            subscriptions { topic: "/image" ros2_data_type: IMAGE }
            subscriptions { topic: "/pcm" ros2_data_type: UINT8_MULTI_ARRAY }
            publishers { topic: "/events" ros2_data_type: STRING }
          }
          width: 640 height: 480 fps: 15 bitrate_kbps: 1500
          stream_name: "baby" listen_address: "127.0.0.1"
          http_port: 8889 ice_port: 8189 rtsp_port: 8554
        }}""",
            config,
        )
        logs = tempfile.TemporaryDirectory()
        self.addCleanup(logs.cleanup)
        environment = patch.dict(
            os.environ, {"ROS_LOG_DIR": logs.name, "ROS_LOCALHOST_ONLY": "1"}
        )
        environment.start()
        self.addCleanup(environment.stop)
        rclpy.init(args=[])
        try:
            with patch("ros2.utils.media_stream_node.MediaStream") as bridge:
                bridge.return_value.url = "http://127.0.0.1:8889/baby/"
                node = MediaStreamNode("media_wire_test", 5, config)
                self.assertEqual(node.video_subscription.qos_profile.depth, 1)
                self.assertEqual(node.audio_subscription.qos_profile.depth, 10)
                node.receive_video(
                    Image(width=2, height=2, step=6, encoding="rgb8", data=bytes(12))
                )
                self.assertEqual(bridge.return_value.submit.call_count, 1)
                node.shutdown()
                bridge.return_value.close.assert_called_once()
                node.destroy_node()
        finally:
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
