import sys

# Add system site-packages for ROS2 compatibility
# This allows Bazel's hermetic Python to find system-installed packages
# like numpy, which is required by ROS2 sensor_msgs
sys.path.insert(0, '/opt/ros/humble/lib/python3.10/site-packages')
sys.path.insert(0, '/opt/ros/humble/local/lib/python3.10/dist-packages')
sys.path.insert(0, '/usr/lib/python3/dist-packages')

import rclpy
from google.protobuf import text_format
from rclpy.node import Node

# Protobuf generated modules
from config.proto import config_pb2


def load_config(config_path: str) -> config_pb2.Config:
    """Load a text-format protobuf Config from file path."""
    cfg = config_pb2.Config()
    with open(config_path) as f:
        text_format.Parse(f.read(), cfg)
    return cfg


def parse_cli(argv: list[str]) -> tuple[str, int, str]:
    """Parse CLI args: <binary> <node_name> <node_id> <config_path>."""
    if len(argv) < 4:
        raise ValueError("Usage: <binary> <node_name> <node_id> <config_path>")
    node_name = argv[1]
    try:
        node_id = int(argv[2])
    except ValueError as e:
        raise ValueError("node_id must be an integer") from e
    config_path = argv[3]
    return node_name, node_id, config_path


def run_node(node_cls: type[Node], logger_name: str, argv: list[str] | None = None) -> int:
    """Run an rclpy Node class with CLI args, mirroring C++ node_runner.

    Args:
        node_cls: Subclass of rclpy.node.Node with __init__(node_name, node_id, config)
        logger_name: Logger name to use for usage/error logging
        argv: Optional argv list; defaults to sys.argv
    """
    if argv is None:
        argv = sys.argv

    rclpy.init(args=argv)

    try:
        node_name, node_id, config_path = parse_cli(argv)
    except Exception as e:
        # Prefer rclpy logger format similar to the C++ variant
        try:
            rclpy.logging.get_logger(logger_name).error(str(e))
        except Exception:
            print(str(e), file=sys.stderr)
        rclpy.shutdown()
        return 1

    try:
        cfg = load_config(config_path)
        node = node_cls(node_name, node_id, cfg)
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            node.destroy_node()  # type: ignore[name-defined]
        except Exception:
            pass
        rclpy.shutdown()

    return 0
