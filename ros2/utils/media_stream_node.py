"""ROS image/audio subscribers for the private browser streaming utility."""

import json
import queue
import time

from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from robot.perception.proto.perception_packet_pb2 import PerceptionPacket
from sensor_msgs.msg import Image
from std_msgs.msg import String, UInt8MultiArray

from ros2.node_runner import run_node
from ros2.proto import node_pb2, ros2_data_type_pb2
from ros2.utils.media_stream import MediaStream, Sample, validate_options


def image_sample(message, fps):
    if (
        message.encoding not in ("rgb8", "bgr8")
        or not 1 <= message.width <= 4096
        or not 1 <= message.height <= 4096
        or message.step < message.width * 3
        or len(message.data) != message.step * message.height
    ):
        raise ValueError("Invalid RGB/BGR image dimensions or payload")
    data = bytes(message.data)
    row_bytes = message.width * 3
    # GStreamer RGB rows are aligned to four bytes, unlike arbitrary ROS step.
    stride = (row_bytes + 3) & ~3
    if message.step != stride:
        padding = bytes(stride - row_bytes)
        data = b"".join(
            data[row * message.step : row * message.step + row_bytes] + padding
            for row in range(message.height)
        )
    stamp = message.header.stamp.sec * 1_000_000_000 + message.header.stamp.nanosec
    color = "RGB" if message.encoding == "rgb8" else "BGR"
    return Sample(
        "video",
        stamp,
        data,
        f"video/x-raw,format={color},width={message.width},height={message.height},"
        f"framerate={fps}/1",
        1_000_000_000 // fps,
    )


def audio_sample(message):
    if len(message.data) > 300000:
        raise ValueError("Oversized audio packet")
    packet = PerceptionPacket.FromString(bytes(message.data))
    a = packet.audio
    if (
        packet.WhichOneof("data_type") != "audio"
        or a.encoding != "s16le"
        or not 8000 <= a.sample_rate_hz <= 48000
        or not 1 <= a.channels <= 2
        or not 0 < a.frames <= a.sample_rate_hz // 5
        or len(a.data) != a.frames * a.channels * 2
    ):
        raise ValueError("Expected <=200 ms of interleaved s16le PCM")
    return Sample(
        "audio",
        packet.timestamp_ns,
        a.data,
        f"audio/x-raw,format=S16LE,layout=interleaved,rate={a.sample_rate_hz},"
        f"channels={a.channels}",
        a.frames * 1_000_000_000 // a.sample_rate_hz,
        a.discontinuity,
    )


class MediaStreamNode(Node):
    def __init__(self, name, node_id, config):
        super().__init__(name)
        matches = [s for s in config.utilities.media_streams if s.node.id == node_id]
        if len(matches) != 1 or matches[0].node.node_type != node_pb2.MEDIA_STREAM_NODE:
            raise ValueError("Exactly one MEDIA_STREAM_NODE configuration is required")
        self.options = matches[0]
        validate_options(self.options)
        n = self.options.node
        images = [
            s for s in n.subscriptions if s.ros2_data_type == ros2_data_type_pb2.IMAGE
        ]
        audio = [
            s
            for s in n.subscriptions
            if s.ros2_data_type == ros2_data_type_pb2.UINT8_MULTI_ARRAY
        ]
        topics = [s.topic for s in n.subscriptions] + [p.topic for p in n.publishers]
        if (
            len(images) != 1
            or len(audio) > 1
            or len(n.subscriptions) != len(images) + len(audio)
            or len(n.publishers) != 1
            or n.publishers[0].ros2_data_type != ros2_data_type_pb2.STRING
            or any(not topic for topic in topics)
            or len(set(topics)) != len(topics)
        ):
            raise ValueError(
                "Media requires image, optional audio, and a distinct STRING output"
            )
        self.events = queue.Queue(maxsize=20)
        self.output = self.create_publisher(String, n.publishers[0].topic, 10)
        self.stream = MediaStream(self.options, bool(audio), self.report)
        self.last_error = 0
        self.last_report = None
        self.last_heartbeat = 0
        self.video_subscription = self.create_subscription(
            Image,
            images[0].topic,
            self.receive_video,
            QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT),
        )
        if audio:
            self.audio_subscription = self.create_subscription(
                UInt8MultiArray,
                audio[0].topic,
                self.receive_audio,
                QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT),
            )
        self.timer = self.create_timer(0.5, self.flush_events)
        try:
            self.stream.start()
        except Exception:
            self.stream.close()
            raise
        self.get_logger().info(f"Private live player: {self.stream.url}")

    def report(self, state, detail):
        try:
            self.events.put_nowait((state, detail))
        except queue.Full:
            pass

    def flush_events(self):
        # Periodic state lets downstream monitors detect a dead streaming process.
        now = time.monotonic()
        if self.events.empty() and self.last_report and now - self.last_heartbeat >= 5:
            self.report(*self.last_report)
        while not self.events.empty():
            state, detail = self.events.get_nowait()
            changed = self.last_report != (state, detail)
            self.last_report = (state, detail)
            self.last_heartbeat = now
            self.output.publish(
                String(
                    data=json.dumps(
                        {
                            "schema_version": 1,
                            "source": self.get_name(),
                            "event_type": state,
                            "observed_at_ns": time.time_ns(),
                            "summary": detail,
                            "url": self.stream.url,
                            "dropped_video": self.stream.pending.dropped_video,
                            "dropped_audio": self.stream.pending.dropped_audio,
                            "recording": (
                                self.stream.recorder.status
                                if self.stream.recorder
                                else {"state": "disabled"}
                            ),
                        }
                    )
                )
            )
            if changed:
                self.get_logger().info(f"{state}: {detail}")

    def invalid(self, error):
        if time.monotonic() - self.last_error > 5:
            self.last_error = time.monotonic()
            self.get_logger().warning(str(error))

    def receive_video(self, message):
        try:
            self.stream.submit(image_sample(message, self.options.fps))
        except Exception as error:
            self.invalid(error)

    def receive_audio(self, message):
        try:
            self.stream.submit(audio_sample(message))
        except Exception as error:
            self.invalid(error)

    def shutdown(self):
        self.stream.close()


if __name__ == "__main__":
    raise SystemExit(run_node(MediaStreamNode, "media_stream_node"))
