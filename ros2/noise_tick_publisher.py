"""Timer-based publisher to drive RandomNoise inputs."""
from __future__ import annotations

import sys
from typing import Optional

import rclpy
from rclpy.node import Node
from std_msgs.msg import Empty


class NoiseTickPublisher(Node):
    """Publishes std_msgs/Empty to spike/noise_tick at a fixed rate."""

    def __init__(self, node_name: str, node_id: int, *_args):
        super().__init__(node_name)
        topic = "spike/noise_tick"
        self._publisher = self.create_publisher(Empty, topic, 10)
        self._timer = self.create_timer(0.1, self._publish)  # 10 Hz
        self.get_logger().info(f"NoiseTickPublisher started on {topic} at 10 Hz")

    def _publish(self) -> None:
        self._publisher.publish(Empty())


def main(argv: Optional[list[str]] = None) -> int:
    """Run the tick publisher. Config path is optional/ignored."""
    if argv is None:
        argv = sys.argv

    # Accept optional args: <binary> <node_name> <node_id> [config_path]
    node_name = argv[1] if len(argv) > 1 else "noise_tick_publisher"
    try:
        node_id = int(argv[2]) if len(argv) > 2 else 0
    except ValueError:
        node_id = 0

    rclpy.init(args=argv)
    try:
        node = NoiseTickPublisher(node_name, node_id)
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            node.destroy_node()
        except Exception:
            pass
        if rclpy.ok():
            rclpy.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
