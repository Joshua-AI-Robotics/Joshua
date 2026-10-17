"""Subscribe to a running preset and save bounded audio/video evidence."""

import argparse
import json
import time
import wave
from pathlib import Path

import numpy as np
import rclpy
from PIL import Image as PillowImage
from rclpy.node import Node
from robot.perception.proto.perception_packet_pb2 import PerceptionPacket
from sensor_msgs.msg import Image
from std_msgs.msg import String, UInt8MultiArray

from ai.models.video_detection.processor import decode_image
from ai.models.yamnet.processor import AudioWindow
from robot.perception.proto import perception_pb2
from ros2.monitor_common import sensor_qos
from ros2.node_runner import load_config


class CaptureCheck(Node):
    def __init__(self, config, seconds):
        super().__init__("av_check")
        self.audio = []
        self.audio_format = None
        self.max_seconds = seconds
        self.audio_frames = 0
        self.images = 0
        self.latest = None
        self.events = []
        self.errors = []
        self.gaps = 0
        self.window = AudioWindow()
        self.subscriptions_ = []
        for sensor in config.robot.perceptions.single_perceptions:
            if sensor.sensor_type == perception_pb2.AUDIO:
                self.subscriptions_.append(
                    self.create_subscription(
                        UInt8MultiArray,
                        sensor.node.publishers[0].topic,
                        self.on_audio,
                        sensor_qos(100),
                    )
                )
            elif sensor.sensor_type == perception_pb2.IMAGE:
                self.subscriptions_.append(
                    self.create_subscription(
                        Image,
                        sensor.node.publishers[0].topic,
                        self.on_image,
                        sensor_qos(1),
                    )
                )
        for model in config.ai.models.single_models:
            for publisher in model.node.publishers:
                self.subscriptions_.append(
                    self.create_subscription(String, publisher.topic, self.on_event, 10)
                )

    def on_audio(self, message):
        try:
            packet = PerceptionPacket.FromString(bytes(message.data))
            gap = self.window.append(packet)
            if gap and self.audio:
                self.gaps += 1
            audio = packet.audio
            fmt = (audio.sample_rate_hz, audio.channels)
            if self.audio_format is not None and self.audio_format != fmt:
                raise ValueError("Audio format changed during check")
            self.audio_format = fmt
            if (
                self.audio_frames + audio.frames
                <= self.max_seconds * audio.sample_rate_hz
            ):
                self.audio.append(audio.data)
                self.audio_frames += audio.frames
        except Exception as error:
            if len(self.errors) < 20:
                self.errors.append(str(error))

    def on_image(self, message):
        try:
            self.latest = decode_image(message)
            self.images += 1
        except ValueError as error:
            if len(self.errors) < 20:
                self.errors.append(str(error))

    def on_event(self, message):
        if len(self.events) < 1000:
            self.events.append(json.loads(message.data))

    def save(self, destination):
        if self.audio:
            with wave.open(str(destination / "audio.wav"), "wb") as wav:
                wav.setnchannels(self.audio_format[1])
                wav.setsampwidth(2)
                wav.setframerate(self.audio_format[0])
                for chunk in self.audio:
                    wav.writeframesraw(chunk)
        if self.latest is not None:
            PillowImage.fromarray(self.latest).save(destination / "camera.png")
        kinds = sorted({event.get("event_type", "") for event in self.events})
        audio_ok = any(
            kind in kinds
            for kind in ("audio_classification", "cry_started", "cry_ended")
        )
        video_ok = "video_detection" in kinds
        values = np.frombuffer(b"".join(self.audio), dtype="<i2").astype(np.float32)
        summary = {
            "audio_frames": self.audio_frames,
            "camera_frames": self.images,
            "audio_gap_count": self.gaps,
            "audio_rms": float(np.sqrt(np.mean(values**2))) if values.size else None,
            "event_types": kinds,
            "errors": self.errors,
            "passed": bool(
                self.audio
                and self.images
                and audio_ok
                and video_ok
                and not self.errors
                and not self.gaps
                and "inference_error" not in kinds
                and "input_stale" not in kinds
            ),
        }
        (destination / "events.json").write_text(json.dumps(self.events, indent=2))
        (destination / "summary.json").write_text(json.dumps(summary, indent=2))
        print(json.dumps(summary, indent=2))
        return summary["passed"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True)
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    if not 2 <= args.seconds <= 120:
        parser.error("--seconds must be 2..120")
    destination = Path(args.output)
    destination.mkdir(parents=True, exist_ok=False)
    rclpy.init(args=[])
    node = CaptureCheck(load_config(args.config), args.seconds)
    try:
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            rclpy.spin_once(node, timeout_sec=0.1)
        return 0 if node.save(destination) else 1
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
