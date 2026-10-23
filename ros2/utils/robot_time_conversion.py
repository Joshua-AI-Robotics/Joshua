"""Convert ROS timestamp representations without reading or changing clocks."""

from builtin_interfaces.msg import Time


def to_ros_timestamp(timestamp_ns: int) -> Time:
    sec, nanosec = divmod(timestamp_ns, 1_000_000_000)
    if not -(2**31) <= sec < 2**31:
        raise ValueError("Timestamp exceeds ROS seconds range")
    return Time(sec=sec, nanosec=nanosec)


def from_ros_timestamp(timestamp: Time) -> int:
    if not 0 <= timestamp.nanosec < 1_000_000_000:
        raise ValueError("ROS timestamp nanosec must be less than 1e9")
    return timestamp.sec * 1_000_000_000 + timestamp.nanosec
