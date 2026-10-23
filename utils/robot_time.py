"""Python access to the process-global C++ robot clock."""

from utils import _robot_time

robot_time = _robot_time.robot_time
robot_time_ns = _robot_time.robot_time_ns


def initialize_robot_clock(config) -> str:
    """Install the configured source before constructing nodes or workers."""
    return _robot_time.initialize(config.SerializeToString())
