from __future__ import annotations

import math
import time
from dataclasses import dataclass
from typing import Dict, List

from rclpy.node import Node
from sensor_msgs.msg import JointState
from std_msgs.msg import Float32

from config.proto import config_pb2
from robot.action.proto import action_packet_pb2
from ros2.node_runner import run_node
from ros2.proto import node_pb2, ros2_data_type_pb2
from ros2.utils.packet_parser import extract_scalar_from_action
from ros2.utils.qos_setting import create_qos_setting


@dataclass
class TopicPub:
    publisher: object
    data_type: int


@dataclass
class TrajectoryWaypointEntry:
    timestamp_sec: float
    topic: str
    action: action_packet_pb2.ActionPacket


class TrajectoryPublisher(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._waypoints: List[TrajectoryWaypointEntry] = []
        self._topic_pubs: Dict[str, TopicPub] = {}
        self._loop_running = False
        unsupported_topics = set()

        for single_trajectory in config.robot.trajectories.single_trajectories:
            if (
                single_trajectory.node.node_type
                == node_pb2.NodeType.TRAJECTORY_PUBLISHER
                and int(single_trajectory.node.id) == node_id
            ):
                qos_setting = single_trajectory.node.qos_setting
                trajectory = single_trajectory.trajectory

                pub_cfgs: Dict[str, int] = {}
                for pub_cfg in single_trajectory.node.publishers:
                    pub_cfgs[pub_cfg.topic] = pub_cfg.ros2_data_type

                for waypoint in trajectory.waypoints:
                    self._waypoints.append(
                        TrajectoryWaypointEntry(
                            timestamp_sec=waypoint.timestamp_sec,
                            topic=waypoint.topic,
                            action=waypoint.action,
                        )
                    )

                    if waypoint.topic not in self._topic_pubs:
                        data_type = pub_cfgs.get(
                            waypoint.topic,
                            ros2_data_type_pb2.FLOAT32,
                        )
                        message_class = {
                            ros2_data_type_pb2.FLOAT32: Float32,
                            ros2_data_type_pb2.JOINT_STATE: JointState,
                        }.get(data_type)
                        if message_class is None:
                            if waypoint.topic in unsupported_topics:
                                continue
                            unsupported_topics.add(waypoint.topic)
                            self.get_logger().error(
                                f"Unsupported publisher ros2_data_type {data_type} "
                                f"for topic '{waypoint.topic}'. "
                                "Supported types: FLOAT32, JOINT_STATE."
                            )
                            continue
                        pub = self.create_publisher(
                            message_class,
                            waypoint.topic,
                            create_qos_setting(qos_setting),
                        )
                        self._topic_pubs[waypoint.topic] = TopicPub(
                            publisher=pub,
                            data_type=data_type,
                        )

        if not self._waypoints:
            self.get_logger().error(
                f"No trajectory waypoints found for node_id {node_id}!"
            )
            return

        if not self._topic_pubs:
            self.get_logger().error(
                "No supported trajectory publishers; playback will not start."
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
        for topic, entry in self._topic_pubs.items():
            if entry.publisher.get_subscription_count() == 0:
                self.get_logger().info(f"Waiting for subscribers on '{topic}'...")
                return

        self._timer.cancel()
        self.get_logger().info("All topics have subscribers, starting trajectory loop")
        self._loop_timer = self.create_timer(0.0, self._run_trajectory_loop)

    def _joint_state_message(
        self, packet: action_packet_pb2.ActionPacket
    ) -> JointState:
        if not packet.HasField("joint") or not packet.joint.joint_name:
            raise ValueError("JointState requires a named joint command")
        joint = packet.joint
        fields = [f for f in ("position", "velocity", "effort") if joint.HasField(f)]
        if not fields or any(not math.isfinite(getattr(joint, f)) for f in fields):
            raise ValueError("JointState requires finite position, velocity or effort")
        if (
            joint.HasField("position")
            and joint.position_encoding != action_packet_pb2.JointCommand.POSITION_SI
        ):
            raise ValueError(
                "JointState position requires POSITION_SI (radians or meters)"
            )
        if (joint.HasField("velocity") or joint.HasField("effort")) and (
            joint.units != action_packet_pb2.JointCommand.SI
        ):
            raise ValueError("JointState velocity and effort require SI units")
        message = JointState()
        message.name = [joint.joint_name]
        for field in fields:
            setattr(message, field, [getattr(joint, field)])
        message.header.frame_id = joint.frame_id
        if packet.timestamp_ns:
            sec, nanosec = divmod(packet.timestamp_ns, 1_000_000_000)
            if not -(2**31) <= sec < 2**31:
                raise ValueError("Source timestamp exceeds ROS time range")
            message.header.stamp.sec = sec
            message.header.stamp.nanosec = nanosec
        else:
            # Config waypoints normally have no source stamp; use ROS publish time.
            message.header.stamp = self.get_clock().now().to_msg()
        return message

    def _publish_waypoint(self, waypoint: TrajectoryWaypointEntry) -> None:
        topic_pub = self._topic_pubs.get(waypoint.topic)
        if topic_pub is None:
            return

        if topic_pub.data_type == ros2_data_type_pb2.JOINT_STATE:
            try:
                message = self._joint_state_message(waypoint.action)
            except ValueError as exc:
                self.get_logger().error(
                    f"[t={waypoint.timestamp_sec:.3f}s] Invalid JointState "
                    f"on '{waypoint.topic}': {exc}; skipping"
                )
                return
            topic_pub.publisher.publish(message)
            return

        value = extract_scalar_from_action(waypoint.action, waypoint.topic)
        if value is None:
            which = waypoint.action.WhichOneof("action_type") or "none"
            self.get_logger().warning(
                f"[t={waypoint.timestamp_sec:.3f}s] "
                f"Unsupported action type '{which}' for Float32 topic "
                f"{waypoint.topic}, skipping"
            )
            return
        msg = Float32()
        msg.data = value
        topic_pub.publisher.publish(msg)
        self.get_logger().debug(
            f"[t={waypoint.timestamp_sec:.3f}s] {waypoint.topic} -> {value}"
        )

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

                self._publish_waypoint(waypoint)

            self.get_logger().info("Trajectory loop completed, restarting...")

    def shutdown(self):
        self._loop_running = False


def main() -> int:
    return run_node(TrajectoryPublisher, "trajectory_publisher")


if __name__ == "__main__":
    raise SystemExit(main())
