import subprocess
import sys
import unittest


class RobotTimeTest(unittest.TestCase):
    def run_fresh(self, code):
        result = subprocess.run(
            [sys.executable, "-c", code], capture_output=True, text=True, timeout=20
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_configured_source_shared_by_both_accessors(self):
        self.run_fresh(
            """
import time
from config.proto import config_pb2
from utils.robot_time import initialize_robot_clock, robot_time, robot_time_ns
config = config_pb2.Config().general.robot_clock
config.source = config.MONOTONIC
assert initialize_robot_clock(config) == 'MONOTONIC (local only)'
before = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
stamp = robot_time_ns()
after = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
assert isinstance(stamp, int) and before <= stamp <= after
assert before / 1e9 <= robot_time() <= time.clock_gettime(time.CLOCK_MONOTONIC)
try:
    initialize_robot_clock(config)
except RuntimeError as error:
    assert 'already initialized' in str(error)
else:
    raise AssertionError('Clock was replaced')
"""
        )

    def test_failed_initialization_does_not_consume_installation(self):
        self.run_fresh(
            """
from config.proto import config_pb2
from utils.robot_time import initialize_robot_clock
config = config_pb2.Config().general.robot_clock
config.require_ptp = True
config.ptp_utc_offset_seconds = 0
config.ptp_device = '/nonexistent-joshua-test/ptp'
try:
    initialize_robot_clock(config)
except RuntimeError:
    pass
else:
    raise AssertionError('Required missing PTP was accepted')
config.Clear()
config.source = config.UTC
assert initialize_robot_clock(config) == 'UTC'
"""
        )


if __name__ == "__main__":
    unittest.main()
