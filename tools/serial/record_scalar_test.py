"""Deterministic recorder timing tests without ROS discovery or hardware."""

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tools.serial import record_scalar


def observation(sensor="analog_a0"):
    packet = record_scalar.perception_packet_pb2.PerceptionPacket(perception_id=sensor)
    packet.scalar.value = 512
    packet.scalar.usable = True
    return SimpleNamespace(data=packet.SerializeToString())


class RecorderTimingTest(unittest.TestCase):
    def run_recorder(
        self, events, extra_args=(), robot_clock=lambda now: now, config=None
    ):
        now = 0.0
        callbacks = {}
        pending = list(events)
        node = mock.Mock()
        node.create_subscription.side_effect = (
            lambda _, topic, callback, qos: callbacks.update({topic: callback})
        )

        def spin_once(_, timeout_sec):
            nonlocal now
            self.assertLess(now, 30, "Recorder failed to reach its robot-time deadline")
            end = now + timeout_sec
            if pending and pending[0][0] <= end:
                now, topic, message = pending.pop(0)
                callbacks[topic](message)
            else:
                now = end

        ros = mock.Mock()
        ros.create_node.return_value = node
        ros.ok.return_value = True
        ros.spin_once.side_effect = spin_once
        clock = SimpleNamespace(monotonic_ns=lambda: int(now * 1_000_000_000))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "capture.jsonl"
            if config is not None:
                config_path = Path(directory) / "config.pbtxt"
                config_path.write_text(config)
                extra_args = [*extra_args, "--config", str(config_path)]
            argv = [
                "record_scalar",
                "--topic",
                "/observation",
                "--sensor",
                "analog_a0",
                "--seconds",
                "3",
                "--output",
                str(output),
                *extra_args,
            ]
            with (
                mock.patch.object(record_scalar, "rclpy", ros),
                mock.patch.object(record_scalar, "time", clock),
                mock.patch.object(record_scalar, "RobotTime", lambda: robot_clock(now)),
                mock.patch.object(record_scalar, "initialize") as initialize,
                mock.patch("sys.argv", argv),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                try:
                    result = record_scalar.main()
                finally:
                    node.destroy_node.assert_called_once()
                    ros.shutdown.assert_called_once()
                if config is not None:
                    expected = record_scalar.text_format.Parse(
                        config, record_scalar.config_pb2.Config()
                    )
                    initialize.assert_called_once_with(expected.general.robot_clock)
                else:
                    initialize.assert_not_called()
            records = [json.loads(line) for line in output.read_text().splitlines()]
        return result, now, records

    def test_capture_uses_robot_clock_rate_and_config(self):
        result, end, _ = self.run_recorder(
            [(1.0, "/observation", observation())],
            robot_clock=lambda now: 100 + 2 * now,
            config="general { robot_clock { source: UTC } }",
        )
        self.assertEqual(result, 0)
        self.assertAlmostEqual(end, 2.5)

    def test_forward_jump_reaches_capture_deadline_early(self):
        result, end, _ = self.run_recorder(
            [(1.0, "/observation", observation())],
            robot_clock=lambda now: now + (20 if now >= 2 else 0),
        )
        self.assertEqual(result, 0)
        self.assertAlmostEqual(end, 2.0)

    def test_backward_jump_waits_for_original_capture_deadline(self):
        result, end, _ = self.run_recorder(
            [(1.0, "/observation", observation())],
            robot_clock=lambda now: now - (2 if now >= 2 else 0),
        )
        self.assertEqual(result, 0)
        self.assertAlmostEqual(end, 6.0)

    def test_startup_budget_uses_robot_time(self):
        result, end, records = self.run_recorder([], robot_clock=lambda now: 2 * now)
        self.assertEqual(result, 1)
        self.assertAlmostEqual(end, 5.0)
        self.assertTrue(records[-1]["startup_timed_out"])

    def test_clock_failure_propagates_after_ros_cleanup(self):
        def failing_clock(now):
            if now >= 2:
                raise RuntimeError("clock read failed")
            return now

        with self.assertRaisesRegex(RuntimeError, "clock read failed"):
            self.run_recorder(
                [(1.0, "/observation", observation())], robot_clock=failing_clock
            )

    def test_delayed_source_gets_full_capture_window(self):
        result, end, records = self.run_recorder(
            [
                (4.0, "/observation", observation()),
                (6.9, "/observation", observation()),
                (7.1, "/observation", observation()),
            ]
        )
        self.assertEqual(result, 0)
        self.assertAlmostEqual(end, 7.0)
        self.assertEqual(records[-1]["counts"]["observations"], 2)
        self.assertFalse(records[-1]["startup_timed_out"])

    def test_missing_source_has_bounded_startup_wait(self):
        result, end, records = self.run_recorder([])
        self.assertEqual(result, 1)
        self.assertAlmostEqual(end, 10.0)
        self.assertEqual(records[-1]["counts"]["observations"], 0)
        self.assertTrue(records[-1]["startup_timed_out"])

    def test_health_and_wrong_sensor_do_not_start_or_extend_capture(self):
        result, end, records = self.run_recorder(
            [
                (1.0, "/observation/health", SimpleNamespace(data="usable")),
                (4.0, "/observation", observation("different_sensor")),
                (9.0, "/observation", SimpleNamespace(data=b"\xff")),
            ]
        )
        self.assertEqual(result, 1)
        self.assertAlmostEqual(end, 10.0)
        self.assertEqual(records[-1]["counts"]["observations"], 0)
        self.assertEqual(records[-1]["counts"]["malformed"], 2)
        self.assertTrue(records[-1]["startup_timed_out"])

    def test_zero_startup_wait_preserves_immediate_capture(self):
        result, end, records = self.run_recorder(
            [(4.0, "/observation", observation())],
            ["--startup-timeout-seconds", "0"],
        )
        self.assertEqual(result, 1)
        self.assertAlmostEqual(end, 3.0)
        self.assertEqual(records[-1]["counts"]["observations"], 0)
        self.assertFalse(records[-1]["startup_timed_out"])


if __name__ == "__main__":
    unittest.main()
