#!/usr/bin/env python3
import argparse
import math
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Publish sine wave to a ROS topic.")
    parser.add_argument(
        "--topic",
        default="spike/motor_A/command",
        help="ROS topic to publish to",
    )
    parser.add_argument("--offset", type=float, default=90.0, help="Angle offset")
    parser.add_argument("--amplitude", type=float, default=45.0, help="Wave amplitude")
    parser.add_argument("--hz", type=float, default=20.0, help="Publish rate (Hz)")
    parser.add_argument("--omega", type=float, default=1.0, help="Wave speed (rad/s)")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    rclpy.init()
    node = Node("spike_wave")
    pub = node.create_publisher(Float32, args.topic, 10)
    t0 = time.time()
    try:
        while True:
            msg = Float32()
            msg.data = args.offset + args.amplitude * math.sin(
                (time.time() - t0) * args.omega
            )
            pub.publish(msg)
            time.sleep(1.0 / args.hz)
    finally:
        node.destroy_node()
        rclpy.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
