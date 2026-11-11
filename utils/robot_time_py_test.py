"""Exercise the native binding in fresh processes with hardware-free clocks."""

import os
import subprocess
import sys
import textwrap
import unittest


class RobotTimeBindingTest(unittest.TestCase):
    def run_clock(self, script):
        result = subprocess.run(
            [
                sys.executable,
                "-c",
                "from config.proto.robot_clock_pb2 import RobotClockConfig\n"
                "from utils.robot_time import RobotTime, initialize\n"
                "import time\n" + textwrap.dedent(script),
            ],
            env={**os.environ, "PYTHONPATH": os.pathsep.join(sys.path)},
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_utc_uses_platform_epoch(self):
        self.run_clock("""
            initialize(RobotClockConfig(source=RobotClockConfig.UTC))
            before = time.time() - 1750118400
            now = RobotTime()
            after = time.time() - 1750118400
            assert before - 1e-6 <= now <= after + 1e-6, (before, now, after)
            """)

    def test_monotonic_and_install_once(self):
        self.run_clock("""
            config = RobotClockConfig(source=RobotClockConfig.MONOTONIC)
            initialize(config)
            before = time.monotonic()
            now = RobotTime()
            after = time.monotonic()
            assert before <= now <= after, (before, now, after)
            try:
                initialize(config)
            except RuntimeError as error:
                assert 'already initialized' in str(error), str(error)
            else:
                raise AssertionError('Clock was replaced')
            """)

    def test_required_missing_ptp_fails_and_allows_retry(self):
        self.run_clock("""
            try:
                initialize(RobotClockConfig(
                    ptp_device='/dev/joshua-test-nonexistent-ptp', require_ptp=True))
            except RuntimeError:
                pass
            else:
                raise AssertionError('Required PTP silently fell back')
            initialize(RobotClockConfig(source=RobotClockConfig.UTC))
            assert RobotTime() > 0
            """)

    def test_invalid_config_is_rejected_without_installing(self):
        self.run_clock("""
            from utils import _robot_time
            try:
                _robot_time.initialize(b'\\xff')
            except ValueError:
                pass
            else:
                raise AssertionError('Malformed protobuf accepted')
            try:
                initialize(RobotClockConfig(
                    source=RobotClockConfig.UTC, require_ptp=True))
            except RuntimeError:
                pass
            else:
                raise AssertionError('Invalid clock config accepted')
            initialize(RobotClockConfig(source=RobotClockConfig.MONOTONIC))
            assert RobotTime() > 0
            """)


if __name__ == "__main__":
    unittest.main()
