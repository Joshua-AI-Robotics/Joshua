from __future__ import annotations

import base64
import time
from dataclasses import dataclass
from typing import Dict, List

from rclpy.node import Node
from std_msgs.msg import Float32, String, UInt8MultiArray

from config.proto import config_pb2
from robot.action.proto import action_packet_pb2
from ros2.node_runner import run_node
from ros2.proto import node_pb2, ros2_data_type_pb2
from ros2.utils.packet_parser import (
    PacketParseError,
    action_to_perception_position_packet,
    extract_scalar_from_action,
    serialize_action_packet,
    serialize_perception_packet,
)
from ros2.utils.qos_setting import create_qos_setting

_DATA_TYPE_TO_MSG = {
    ros2_data_type_pb2.FLOAT32: Float32,
    ros2_data_type_pb2.STRING: String,
    ros2_data_type_pb2.UINT8_MULTI_ARRAY: UInt8MultiArray,
}


@dataclass
class TopicPub:
    publisher: object
    data_type: int
    payload_type: int


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

        for single_trajectory in config.robot.trajectories.single_trajectories:
            if (
                single_trajectory.node.node_type
                == node_pb2.NodeType.TRAJECTORY_PUBLISHER
                and int(single_trajectory.node.id) == node_id
            ):
                qos_setting = single_trajectory.node.qos_setting
                trajectory = single_trajectory.trajectory

                pub_cfgs: Dict[str, tuple[int, int]] = {}
                for pub_cfg in single_trajectory.node.publishers:
                    pub_cfgs[pub_cfg.topic] = (
                        pub_cfg.ros2_data_type,
                        pub_cfg.payload_type,
                    )

                for waypoint in trajectory.waypoints:
                    self._waypoints.append(
                        TrajectoryWaypointEntry(
                            timestamp_sec=waypoint.timestamp_sec,
                            topic=waypoint.topic,
                            action=waypoint.action,
                        )
                    )

                    if waypoint.topic not in self._topic_pubs:
                        data_type, payload_type = pub_cfgs.get(
                            waypoint.topic,
                            (
                                ros2_data_type_pb2.FLOAT32,
                                node_pb2.PAYLOAD_TYPE_ACTION_PACKET,
                            ),
                        )
                        msg_cls = _DATA_TYPE_TO_MSG.get(data_type, Float32)
                        pub = self.create_publisher(
                            msg_cls,
                            waypoint.topic,
                            create_qos_setting(qos_setting),
                        )
                        self._topic_pubs[waypoint.topic] = TopicPub(
                            publisher=pub,
                            data_type=data_type,
                            payload_type=payload_type,
                        )

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
        for topic, entry in self._topic_pubs.items():
            if entry.publisher.get_subscription_count() == 0:
                self.get_logger().info(f"Waiting for subscribers on '{topic}'...")
                return

        self._timer.cancel()
        self.get_logger().info("All topics have subscribers, starting trajectory loop")
        self._loop_timer = self.create_timer(0.0, self._run_trajectory_loop)

    def _publish_waypoint(self, waypoint: TrajectoryWaypointEntry) -> None:
        topic_pub = self._topic_pubs.get(waypoint.topic)
        if topic_pub is None:
            return

        if topic_pub.data_type == ros2_data_type_pb2.STRING:
            msg = String()
            msg.data = base64.b64encode(waypoint.action.SerializeToString()).decode(
                "ascii"
            )
            topic_pub.publisher.publish(msg)
            action_type = waypoint.action.WhichOneof("action_type") or "none"
            self.get_logger().debug(
                f"[t={waypoint.timestamp_sec:.3f}s] "
                f"{waypoint.topic} -> ActionPacket({action_type})"
            )
        elif topic_pub.data_type == ros2_data_type_pb2.UINT8_MULTI_ARRAY:
            msg = UInt8MultiArray()
            try:
                if topic_pub.payload_type == node_pb2.PAYLOAD_TYPE_PERCEPTION_PACKET:
                    perception = action_to_perception_position_packet(waypoint.action)
                    msg.data = list(serialize_perception_packet(perception))
                else:
                    msg.data = list(serialize_action_packet(waypoint.action))
            except PacketParseError as exc:
                self.get_logger().warning(
                    f"[t={waypoint.timestamp_sec:.3f}s] "
                    f"Failed to serialize waypoint for topic {waypoint.topic}: {exc}"
                )
                return
            topic_pub.publisher.publish(msg)
            action_type = waypoint.action.WhichOneof("action_type") or "none"
            self.get_logger().debug(
                f"[t={waypoint.timestamp_sec:.3f}s] "
                f"{waypoint.topic} -> ActionPacket bytes ({action_type})"
            )
        else:
            value = extract_scalar_from_action(waypoint.action)
            if value is None:
                self.get_logger().warning(
                    f"[t={waypoint.timestamp_sec:.3f}s] "
                    f"Unsupported action type for Float32 topic "
                    f"{waypoint.topic}, skipping"
                )
                return
            msg = Float32()
            msg.data = value
            topic_pub.publisher.publish(msg)
            self.get_logger().debug(
                f"[t={waypoint.timestamp_sec:.3f}s] " f"{waypoint.topic} -> {value}"
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
