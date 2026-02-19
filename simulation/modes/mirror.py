"""Mirror simulation mode -- ROS2 digital twin.

Subscribes to ROS2 Float32 encoder topics and mirrors the real arm's
joint positions in the MuJoCo simulation in real time.
"""

from __future__ import annotations

import threading

import glog
import mujoco
import mujoco.viewer
import numpy as np

from simulation.proto import simulation_pb2
from simulation.sim_engine import SimEngine


def run(engine: SimEngine, config: simulation_pb2.MirrorConfig) -> None:
    import rclpy
    from rclpy.node import Node
    from std_msgs.msg import Float32

    mappings = list(config.topic_mappings)
    if not mappings:
        raise ValueError("MirrorConfig.topic_mappings is required for mirror mode.")

    num_ctrl = engine.num_actuators
    latest_values = np.zeros(num_ctrl)
    lock = threading.Lock()

    class MirrorNode(Node):
        def __init__(self):
            super().__init__("mujoco_mirror")
            for mapping in mappings:
                idx = mapping.actuator_index
                if idx >= num_ctrl:
                    self.get_logger().warning(
                        f"actuator_index {idx} exceeds model's "
                        f"{num_ctrl} actuators; skipping"
                    )
                    continue
                self.create_subscription(
                    Float32,
                    mapping.topic,
                    _make_callback(idx),
                    10,
                )
                self.get_logger().info(f"  actuator[{idx}] <- {mapping.topic}")

    def _make_callback(index: int):
        def callback(msg: Float32):
            with lock:
                latest_values[index] = msg.data

        return callback

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
