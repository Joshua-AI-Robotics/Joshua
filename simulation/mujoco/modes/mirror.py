"""Mirror simulation mode -- ROS2 digital twin.

Subscribes to ROS2 topics as specified in the MirrorConfig topic_mappings
and mirrors the real arm's joint positions in MuJoCo in real time.
Each mapping specifies the actuator index, topic name, and ROS2 data type.
"""

from __future__ import annotations

import threading

import glog
import mujoco
import mujoco.viewer
import numpy as np

from ros2.ros2_type_resolver import resolve_message_class_from_enum
from simulation.mujoco.engine import MuJoCoEngine
from simulation.proto import simulation_pb2


def run(engine: MuJoCoEngine, config: simulation_pb2.MirrorConfig) -> None:
    import rclpy
    from rclpy.node import Node

    mappings = list(config.topic_mappings)
    if not mappings:
        raise ValueError("MirrorConfig.topic_mappings is required for mirror mode.")

    num_ctrl = engine.num_actuators
    latest_values = np.zeros(num_ctrl)
    offsets = np.array([m.offset for m in mappings[:num_ctrl]], dtype=np.float64)
    lock = threading.Lock()

    def _make_callback(index: int):
        def callback(msg):
            with lock:
                latest_values[index] = msg.data + offsets[index]

        return callback

    class MirrorNode(Node):
        def __init__(self):
            super().__init__("mujoco_mirror")
            for idx, mapping in enumerate(mappings):
                if idx >= num_ctrl:
                    self.get_logger().warning(
                        f"mapping index {idx} exceeds model's "
                        f"{num_ctrl} actuators; skipping"
                    )
                    continue
                msg_cls = resolve_message_class_from_enum(mapping.ros2_data_type)
                self.create_subscription(
                    msg_cls,
                    mapping.topic,
                    _make_callback(idx),
                    10,
                )
                self.get_logger().info(
                    f"  actuator[{idx}] <- {mapping.topic} ({msg_cls.__name__})"
                )

    rclpy.init()
    node = MirrorNode()
    spin_thread = threading.Thread(target=rclpy.spin, args=(node,), daemon=True)
    spin_thread.start()
    glog.info("Mirror mode: waiting for ROS2 messages...")

    try:
        with mujoco.viewer.launch_passive(engine.model, engine.data) as viewer:
            while viewer.is_running():
                with lock:
                    engine.data.ctrl[:num_ctrl] = latest_values

                engine.step()
                viewer.sync()
    finally:
        node.destroy_node()
        rclpy.shutdown()
