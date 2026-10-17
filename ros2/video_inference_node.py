"""Independent CPU video inference process; consumes CameraPublisher images."""

import time

from rclpy.node import Node
from sensor_msgs.msg import Image

from ai.models.video_detection.processor import VideoDetector, decode_image
from ai.proto import ai_model_pb2
from ros2.monitor_common import EventOutput, model_config, sensor_qos
from ros2.node_runner import run_node
from ros2.proto import node_pb2, ros2_data_type_pb2


class VideoInferenceNode(Node):
    def __init__(self, name, node_id, config):
        super().__init__(name)
        model = model_config(
            config,
            node_id,
            ai_model_pb2.VIDEO_DETECTION,
            node_pb2.VIDEO_INFERENCE_NODE,
            ros2_data_type_pb2.IMAGE,
        )
        options = model.video_detection_config
        if (
            not 0 < options.score_threshold <= 1
            or not 0 < options.inference_rate_hz <= 15
            or not 1 <= options.num_threads <= 8
            or not 0 < options.motion_fraction_threshold <= 1
        ):
            raise ValueError("Invalid video inference settings")
        self.model = VideoDetector(options.model_path, options.num_threads)
        self.options = options
        self.latest = None
        self.output = EventOutput(self, model.node.publishers[0].topic)
        self.subscription = self.create_subscription(
            Image, model.node.subscriptions[0].topic, self.receive, sensor_qos(1)
        )
        self.timer = self.create_timer(1.0 / options.inference_rate_hz, self.process)
        self.get_logger().info("EfficientDet Lite0 ready: waiting for camera frames")

    def receive(self, message):
        self.latest = message

    def process(self):
        message, self.latest = self.latest, None
        if message is None:
            if time.monotonic() - self.output.last_input > 2:
                self.model.previous = None
            return
        try:
            stamp = (
                message.header.stamp.sec * 1_000_000_000 + message.header.stamp.nanosec
            )
            age = time.time_ns() - stamp
            if age > 2_000_000_000 or age < -1_000_000_000:
                raise ValueError("Stale camera frame or incompatible clock")
            detections, motion = self.model.infer(
                decode_image(message), self.options.score_threshold
            )
            self.output.received()
            self.output.emit(
                "video_detection",
                stamp,
                detections=detections,
                motion_fraction=motion,
                movement=motion >= self.options.motion_fraction_threshold,
            )
        except Exception as error:
            self.model.previous = None
            self.output.emit("inference_error", severity="warning", summary=str(error))
            self.get_logger().error(str(error))


if __name__ == "__main__":
    raise SystemExit(run_node(VideoInferenceNode, "video_inference_node"))
