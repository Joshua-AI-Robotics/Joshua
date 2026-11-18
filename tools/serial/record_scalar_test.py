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
    def run_recorder(self, events, extra_args=()):
        now = 0.0
        callbacks = {}
        pending = list(events)
        node = mock.Mock()
        node.create_subscription.side_effect = (
            lambda _, topic, callback, qos: callbacks.update({topic: callback})
        )

        def spin_once(_, timeout_sec):
            nonlocal now
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
        clock = SimpleNamespace(
            monotonic=lambda: now, monotonic_ns=lambda: int(now * 1_000_000_000)
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "capture.jsonl"
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
                mock.patch("sys.argv", argv),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                result = record_scalar.main()
            records = [json.loads(line) for line in output.read_text().splitlines()]
        node.destroy_node.assert_called_once()
        ros.shutdown.assert_called_once()
        return result, now, records

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
