"""Independent YAMNet process consuming Joshua audio packets."""

import time

from rclpy.node import Node
from robot.perception.proto.perception_packet_pb2 import PerceptionPacket
from std_msgs.msg import UInt8MultiArray

from ai.models.yamnet.processor import AudioWindow, Yamnet
from ai.proto import ai_model_pb2
from ros2.monitor_common import EventOutput, model_config, sensor_qos
from ros2.node_runner import run_node
from ros2.proto import node_pb2, ros2_data_type_pb2


class YamnetNode(Node):
    def __init__(self, name, node_id, config):
        super().__init__(name)
        model = model_config(
            config,
            node_id,
            ai_model_pb2.YAMNET,
            node_pb2.YAMNET_NODE,
            ros2_data_type_pb2.UINT8_MULTI_ARRAY,
        )
        options = model.yamnet_config
        if (
            not 0 < options.cry_threshold <= 1
            or not 1 <= options.persistence_windows <= 100
            or not 1 <= options.num_threads <= 8
        ):
            raise ValueError("Invalid YAMNet threshold, persistence, or threads")
        self.model = Yamnet(options.model_path, options.num_threads)
        self.window = AudioWindow()
        self.threshold = options.cry_threshold
        self.persistence = options.persistence_windows
        self.positive = 0
        self.active = False
        self.output = EventOutput(self, model.node.publishers[0].topic)
        self.subscription = self.create_subscription(
            UInt8MultiArray,
            model.node.subscriptions[0].topic,
            self.receive,
            sensor_qos(100),
        )
        self.timer = self.create_timer(0.1, self.process)
        self.get_logger().info("YAMNet ready: waiting for 16 kHz PCM")

    def receive(self, message):
        try:
            if len(message.data) > 300000:
                raise ValueError("Oversized audio packet")
            packet = PerceptionPacket.FromString(bytes(message.data))
            age = time.time_ns() - packet.timestamp_ns
            if age > 2_000_000_000 or age < -1_000_000_000:
                raise ValueError("Stale audio or incompatible clock")
            if self.window.append(packet):
                self.positive = 0
                self.active = False
            self.output.received()
        except Exception as error:
            self.window.reset()
            self.positive = 0
            self.active = False
            self.get_logger().error(str(error))

    def process(self):
        if time.monotonic() - self.output.last_input > 2:
            self.window.reset()
            self.positive = 0
            self.active = False
            return
        waveform = self.window.take()
        if waveform is None:
            return
        try:
            score, labels = self.model.infer(waveform)
            self.positive = self.positive + 1 if score >= self.threshold else 0
            active = self.positive >= self.persistence
            kind = (
                "cry_started" if active and not self.active else "audio_classification"
            )
            if self.active and not active:
                kind = "cry_ended"
            self.active = active
            self.output.emit(
                kind, self.window.end_ns, score=score, crying=active, top_labels=labels
            )
        except Exception as error:
            self.output.emit("inference_error", severity="warning", summary=str(error))
            self.get_logger().error(str(error))


if __name__ == "__main__":
    raise SystemExit(run_node(YamnetNode, "yamnet_node"))
