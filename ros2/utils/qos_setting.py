import logging
from typing import Optional

from rclpy.duration import Duration
from rclpy.qos import (
    DurabilityPolicy,
    HistoryPolicy,
    LivelinessPolicy,
    QoSProfile,
    ReliabilityPolicy,
)

from ros2.proto import node_pb2


def create_qos_setting(qos_setting: node_pb2.QosSetting) -> QoSProfile:
    """
    Return QoS object from QosSetting proto message at 'ros2/proto/node.proto'.
    """

    depth = qos_setting.depth
    if depth <= 0:
        logging.warning("Depth is not set. Using default depth 10.")
        depth = 10

    qos_profile = QoSProfile(depth=depth)

    # 1. Reliability
    if qos_setting.reliability_policy == node_pb2.QOS_RELIABILITY_POLICY_RELIABLE:
        qos_profile.reliability = ReliabilityPolicy.RELIABLE
    elif qos_setting.reliability_policy == node_pb2.QOS_RELIABILITY_POLICY_BEST_EFFORT:
        qos_profile.reliability = ReliabilityPolicy.BEST_EFFORT
    else:
        logging.warning(
            f"Invalid reliability policy: {node_pb2.QosReliabilityPolicy.Name(qos_setting.reliability_policy)}. Using default reliable policy."
        )
        qos_profile.reliability = ReliabilityPolicy.RELIABLE

    # 2. Durability
    if qos_setting.durability_policy == node_pb2.QOS_DURABILITY_POLICY_TRANSIENT_LOCAL:
        qos_profile.durability = DurabilityPolicy.TRANSIENT_LOCAL
    elif qos_setting.durability_policy == node_pb2.QOS_DURABILITY_POLICY_VOLATILE:
        qos_profile.durability = DurabilityPolicy.VOLATILE
    else:
        logging.warning(
            f"Invalid durability policy: {node_pb2.QosDurabilityPolicy.Name(qos_setting.durability_policy)}. Using default volatile policy."
        )
        qos_profile.durability = DurabilityPolicy.VOLATILE

    # 3. History
    if qos_setting.history_policy == node_pb2.QOS_HISTORY_POLICY_KEEP_LAST:
        qos_profile.history = HistoryPolicy.KEEP_LAST
    elif qos_setting.history_policy == node_pb2.QOS_HISTORY_POLICY_KEEP_ALL:
        qos_profile.history = HistoryPolicy.KEEP_ALL
    else:
        logging.warning(
            f"Invalid history policy: {node_pb2.QosHistoryPolicy.Name(qos_setting.history_policy)}. Using default keep last policy."
        )
        qos_profile.history = HistoryPolicy.KEEP_LAST

    # 4. Depth (Already handled in constructor)

    # 5. Liveliness
    if qos_setting.liveliness_policy == node_pb2.QOS_LIVELINESS_POLICY_AUTOMATIC:
        qos_profile.liveliness = LivelinessPolicy.AUTOMATIC
    elif (
        qos_setting.liveliness_policy == node_pb2.QOS_LIVELINESS_POLICY_MANUAL_BY_TOPIC
    ):
        qos_profile.liveliness = LivelinessPolicy.MANUAL_BY_TOPIC
    else:
        logging.warning(
            f"Invalid liveliness policy: {node_pb2.QosLivelinessPolicy.Name(qos_setting.liveliness_policy)}. Using default liveliness policy."
        )
        qos_profile.liveliness = LivelinessPolicy.AUTOMATIC

    # 6. Liveliness Lease Duration
    if qos_setting.liveliness_lease_duration_nanoseconds > 0:
        qos_profile.liveliness_lease_duration = Duration(
            nanoseconds=qos_setting.liveliness_lease_duration_nanoseconds
        )

    # 7. Deadline
    if qos_setting.deadline_nanoseconds > 0:
        qos_profile.deadline = Duration(nanoseconds=qos_setting.deadline_nanoseconds)

    # 8. Lifespan
    if qos_setting.lifespan_nanoseconds > 0:
        qos_profile.lifespan = Duration(nanoseconds=qos_setting.lifespan_nanoseconds)

    return qos_profile
