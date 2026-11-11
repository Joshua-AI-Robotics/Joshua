"""Shared C++ robot clock; initialize before the first RobotTime call."""

from utils._robot_time import RobotTime, RobotTimeError  # noqa: F401
from utils._robot_time import initialize as _initialize


def initialize(clock_config):
    """Install a clock from general.robot_clock, without opening robot devices."""
    _initialize(clock_config.SerializeToString())
