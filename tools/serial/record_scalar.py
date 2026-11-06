"""Observe named scalar packets over ROS; never open a board or send commands."""

import argparse
import json
import math
import time
from pathlib import Path

from google.protobuf.json_format import MessageToDict

from robot.perception.proto import perception_packet_pb2
from ros2.utils.ros_python_paths import setup_import_paths

setup_import_paths()
import rclpy  # noqa: E402
from rclpy.qos import QoSProfile, ReliabilityPolicy  # noqa: E402
from std_msgs.msg import String, UInt8MultiArray  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--topic", required=True)
    parser.add_argument("--sensor", required=True)
    parser.add_argument(
        "--output", type=Path, required=True, help="New JSONL file; never overwritten"
    )
    parser.add_argument(
        "--seconds", type=int, default=60, choices=range(1, 3601), metavar="1..3600"
    )
    args = parser.parse_args()
    rclpy.init()
    node = rclpy.create_node("joshua_scalar_recorder_" + str(time.monotonic_ns()))
    counts = {
        "observations": 0,
        "usable": 0,
        "invalid_or_stale": 0,
        "malformed": 0,
        "health": 0,
    }
    written = 0
    exhausted = False
    try:
        with args.output.open("x") as output:

            def record(value):
                nonlocal written, exhausted
                line = json.dumps(value, allow_nan=False) + "\n"
                if written + len(line.encode()) > 64 * 1024 * 1024:
                    exhausted = True
                    return
                output.write(line)
                written += len(line.encode())

            def receive(message):
                if len(message.data) > 4096:
                    counts["malformed"] += 1
                    return
                packet = perception_packet_pb2.PerceptionPacket()
                try:
                    packet.ParseFromString(bytes(message.data))
                except Exception:
                    counts["malformed"] += 1
                    return
                if not packet.HasField("scalar") or packet.perception_id != args.sensor:
                    counts["malformed"] += 1
                    return
                fields = {
                    f.name: getattr(packet.scalar, f.name)
                    for f in packet.scalar.DESCRIPTOR.fields
                }
                if not math.isfinite(fields["value"]):
                    fields["value"] = None
                counts["observations"] += 1
                counts["usable" if packet.scalar.usable else "invalid_or_stale"] += 1
                record(
                    {
                        "kind": "observation",
                        "recorder_steady_ns": time.monotonic_ns(),
                        "sensor": packet.perception_id,
                        "scalar": fields,
                        "sample_metadata": MessageToDict(
                            packet.sample_metadata, preserving_proto_field_name=True
                        ),
                    }
                )

            def health(message):
                counts["health"] += 1
                record(
                    {
                        "kind": "health",
                        "recorder_steady_ns": time.monotonic_ns(),
                        "state": message.data[:4096],
                    }
                )

            # Receive from either reliable or best-effort observation publishers.
            observation_qos = QoSProfile(
                depth=10, reliability=ReliabilityPolicy.BEST_EFFORT
            )
            node.create_subscription(
                UInt8MultiArray, args.topic, receive, observation_qos
            )
            node.create_subscription(String, args.topic + "/health", health, 10)
            end = time.monotonic() + args.seconds
            while rclpy.ok() and not exhausted and time.monotonic() < end:
                rclpy.spin_once(
                    node, timeout_sec=min(0.1, max(0, end - time.monotonic()))
                )
            record(
                {
                    "kind": "summary",
                    "counts": counts,
                    "capacity_exhausted": exhausted,
                    "scope": (
                        "ROS receipt observations; no physical or "
                        "cross-clock age verification"
                    ),
                }
            )
    finally:
        node.destroy_node()
        rclpy.shutdown()
    print(json.dumps({"counts": counts, "capacity_exhausted": exhausted}))
    return (
        0 if counts["observations"] and not exhausted and not counts["malformed"] else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
