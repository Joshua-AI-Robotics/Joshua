from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Dict, List

from rclpy.node import Node
from std_msgs.msg import Float32

from config.proto import config_pb2
from robot.action.proto import action_packet_pb2
from ros2.node_runner import run_node
from ros2.proto import node_pb2
from ros2.utils.qos_setting import create_qos_setting


@dataclass
class TrajectoryWaypointEntry:
    timestamp_sec: float
    topic: str
    action: action_packet_pb2.ActionPacket


def _extract_float_value(action: action_packet_pb2.ActionPacket) -> float | None:
    """Extract the float value from whichever oneof field is set."""
    which = action.WhichOneof("action_type")
    if which == "position":
        return action.position
    if which == "speed":
        return action.speed
    if which == "torque":
        return action.torque
    return None


class TrajectoryPublisher(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._waypoints: List[TrajectoryWaypointEntry] = []
        self._topic_pubs: Dict[str, object] = {}
        self._loop_running = False

        for single_trajectory in config.robot.trajectories.single_trajectories:
            if (
                single_trajectory.node.node_type
                == node_pb2.NodeType.TRAJECTORY_PUBLISHER
                and int(single_trajectory.node.id) == node_id
            ):
                qos_setting = single_trajectory.node.qos_setting
                trajectory = single_trajectory.trajectory

                for waypoint in trajectory.waypoints:
                    self._waypoints.append(
                        TrajectoryWaypointEntry(
                            timestamp_sec=waypoint.timestamp_sec,
                            topic=waypoint.topic,
                            action=waypoint.action,
                        )
                    )

                    if waypoint.topic not in self._topic_pubs:
                        pub = self.create_publisher(
                            Float32,
                            waypoint.topic,
                            create_qos_setting(qos_setting),
                        )
                        self._topic_pubs[waypoint.topic] = pub

        if not self._waypoints:
            self.get_logger().error(
                f"No trajectory waypoints found for node_id {node_id}!"
            )
            return

        self._waypoints.sort(key=lambda w: w.timestamp_sec)

        total_duration = self._waypoints[-1].timestamp_sec
        self.get_logger().info(
            f"Trajectory publisher started with {len(self._waypoints)} waypoints "
            f"across {len(self._topic_pubs)} topics. "
            f"Loop duration: {total_duration:.3f}s"
        )

        self._loop_running = True
        self._timer = self.create_timer(1.0, self._wait_for_subscribers)

    def _wait_for_subscribers(self) -> None:
        for topic, pub in self._topic_pubs.items():
            if pub.get_subscription_count() == 0:
                self.get_logger().info(
                    f"Waiting for subscribers on '{topic}'..."
                )
                return

        self._timer.cancel()
        self.get_logger().info("All topics have subscribers, starting trajectory loop")
        self._loop_timer = self.create_timer(0.0, self._run_trajectory_loop)

    def _run_trajectory_loop(self) -> None:
        self._loop_timer.cancel()

        while self._loop_running:
            loop_start = time.monotonic()

            for waypoint in self._waypoints:
                if not self._loop_running:
                    return

                target_time = loop_start + waypoint.timestamp_sec
                now = time.monotonic()
                sleep_duration = target_time - now
                if sleep_duration > 0:
                    time.sleep(sleep_duration)

                value = _extract_float_value(waypoint.action)
                if value is None:
                    self.get_logger().warning(
                        f"[t={waypoint.timestamp_sec:.3f}s] "
                        f"Unsupported action type for {waypoint.topic}, skipping"
                    )
                    continue

                msg = Float32()
                msg.data = value
                self._topic_pubs[waypoint.topic].publish(msg)
                self.get_logger().debug(
                    f"[t={waypoint.timestamp_sec:.3f}s] "
                    f"{waypoint.topic} -> {value}"
                )

            self.get_logger().info("Trajectory loop completed, restarting...")

    def shutdown(self):
        self._loop_running = False


def main() -> int:
    return run_node(TrajectoryPublisher, "trajectory_publisher")


if __name__ == "__main__":
    raise SystemExit(main())
