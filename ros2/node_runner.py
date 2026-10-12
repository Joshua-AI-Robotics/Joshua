import sys

from ros2.utils.ros_python_paths import setup_import_paths

setup_import_paths()

import rclpy  # noqa: E402
from google.protobuf import text_format  # noqa: E402
from rclpy.node import Node  # noqa: E402

# Protobuf generated modules
from config.proto import config_pb2  # noqa: E402
from config.ros2_network import network_environment  # noqa: E402


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


def run_node(
    node_cls: type[Node], logger_name: str, argv: list[str] | None = None
) -> int:
    """Run an rclpy Node class with CLI args, mirroring C++ node_runner.

    Args:
        node_cls: Subclass of rclpy.node.Node with __init__(node_name,
            node_id, config)
        logger_name: Logger name to use for usage/error logging
        argv: Optional argv list; defaults to sys.argv
    """
    if argv is None:
        argv = sys.argv

    try:
        node_name, node_id, config_path = parse_cli(argv)
        cfg = load_config(config_path)
        with network_environment(cfg.general.ros2_network):
            rclpy.init(args=argv)
            node = None
            try:
                node = node_cls(node_name, node_id, cfg)
                rclpy.spin(node)
            except KeyboardInterrupt:
                pass
            finally:
                try:
                    if node is not None:
                        if hasattr(node, "shutdown"):
                            node.shutdown()
                        node.destroy_node()
                finally:
                    if rclpy.ok():
                        rclpy.shutdown()
    except Exception as exc:
        print(f"{logger_name}: {exc}", file=sys.stderr)
        return 1
    return 0
