"""Policy and real HTTP delivery tests without microphones or cameras."""

import json
import os
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest.mock import patch

from config.proto.util_pb2 import Notification

from ros2.utils.notification import EventPolicy, HomeAssistantSink, validate_options


def options(**overrides):
    values = dict(
        home_assistant_url="http://127.0.0.1:8123",
        webhook_file_env="JOSHUA_TEST_WEBHOOK_FILE",
        request_timeout_sec=1,
        max_event_age_sec=30,
        cooldown_sec=60,
        movement_persistence_sec=2,
        input_timeout_sec=15,
    )
    values.update(overrides)
    return Notification(**values)


class PolicyTest(unittest.TestCase):
    def setUp(self):
        self.now = 100.0
        self.counter = 0
        self.policy = EventPolicy(
            options(),
            ["audio", "video", "media"],
            wall=lambda: int(self.now * 1e9),
            clock=lambda: self.now,
        )

    def event(self, topic, kind, **extra):
        self.counter += 1
        obj = dict(
            schema_version=1,
            event_id=str(self.counter),
            observed_at_ns=int(self.now * 1e9),
            event_type=kind,
            **extra,
        )
        return self.policy.receive(topic, json.dumps(obj))

    def test_cry_episode_dedup_cooldown_and_stale_rejection(self):
        event = dict(
            schema_version=1,
            event_id="cry1",
            event_type="cry_started",
            observed_at_ns=int(self.now * 1e9),
        )
        first = self.policy.receive("audio", json.dumps(event))
        self.assertTrue(first["notify"])
        self.assertTrue(first["crying"])
        self.assertIsNone(self.policy.receive("audio", json.dumps(event)))
        self.assertFalse(self.event("audio", "cry_started")["notify"])
        self.assertFalse(self.event("audio", "cry_ended")["crying"])
        self.now += 61
        self.assertTrue(self.event("audio", "cry_started")["notify"])
        with self.assertRaises(ValueError):
            self.policy.receive("audio", json.dumps(event))

    def test_sustained_motion_and_reset(self):
        self.assertFalse(
            self.event("video", "video_detection", movement=True)["notify"]
        )
        self.now += 1
        self.assertFalse(
            self.event("video", "video_detection", movement=True)["notify"]
        )
        self.now += 1
        self.assertTrue(self.event("video", "video_detection", movement=True)["notify"])
        self.now += 61
        self.assertFalse(
            self.event("video", "video_detection", movement=True)["notify"]
        )
        self.event("video", "video_detection", movement=False)
        self.event("video", "video_detection", movement=True)
        self.now += 2
        self.assertTrue(self.event("video", "video_detection", movement=True)["notify"])

    def test_missing_process_and_recovery(self):
        self.event("audio", "audio_classification", crying=False)
        self.now += 16
        events = self.policy.tick()
        self.assertEqual(len(events), 3)
        self.assertIsNone(events[-1]["crying"])
        self.assertFalse(self.policy.tick())
        restored = self.event("audio", "audio_classification", crying=False)
        self.assertTrue(restored["notify"])
        self.assertEqual(restored["kind"], "input_restored")

    def test_error_state_is_not_recovered_by_transport_heartbeat(self):
        self.event("video", "video_detection", movement=True)
        self.assertTrue(self.event("video", "inference_error")["notify"])
        self.assertIsNone(self.policy.moving)
        self.assertFalse(self.event("video", "inference_error")["notify"])
        self.assertEqual(self.policy.envelope()["inputs"]["video"], "inference_error")

    def test_malformed_nonfinite_future_oversize(self):
        for raw in [
            "[]",
            "{",
            "x" * 65537,
            json.dumps(
                dict(
                    schema_version=1,
                    event_type="video_detection",
                    observed_at_ns=int(self.now * 1e9),
                    movement=1,
                )
            ),
            json.dumps(
                dict(
                    schema_version=1,
                    event_type="streaming",
                    observed_at_ns=float("nan"),
                )
            ),
            json.dumps(
                dict(
                    schema_version=1,
                    event_type="streaming",
                    observed_at_ns=int((self.now + 2) * 1e9),
                )
            ),
        ]:
            with self.assertRaises((ValueError, TypeError)):
                self.policy.receive("video", raw)
        self.assertEqual(self.policy.seen_at, {})

    def test_endpoint_policy(self):
        for url in [
            "http://example.com",
            "http://127.0.0.1/path",
            "https://user:password@example.com",
            "file:///tmp/secret",
        ]:
            with self.assertRaises(ValueError):
                validate_options(options(home_assistant_url=url))
        validate_options(options(home_assistant_url="https://ha.example.com"))


class DeliveryTest(unittest.TestCase):
    def test_retry_http_acceptance_and_secret_not_logged(self):
        received = []
        reports = []
        accepted = threading.Event()

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                received.append(
                    (
                        self.path,
                        json.loads(
                            self.rfile.read(int(self.headers["Content-Length"]))
                        ),
                    )
                )
                self.send_response(503 if len(received) == 1 else 200)
                self.end_headers()

            def log_message(self, *args):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        def report(state, detail):
            reports.append((state, detail))
            if state == "accepted":
                accepted.set()

        sink = HomeAssistantSink(
            options(home_assistant_url=f"http://127.0.0.1:{server.server_port}"), report
        )
        try:
            with tempfile.TemporaryDirectory() as directory:
                secret = "a" * 64
                path = Path(directory) / "secret"
                path.write_text(secret)
                with patch.dict(os.environ, {"JOSHUA_TEST_WEBHOOK_FILE": str(path)}):
                    policy = EventPolicy(options(), ["audio"])
                    payload = policy.envelope(kind="test", notify=True)
                    sink.start()
                    sink.submit(payload)
                    self.assertTrue(accepted.wait(5), reports)
                    sink.close()
                    self.assertEqual(len(received), 2)
                    self.assertEqual(
                        received[0][1]["event_id"], received[1][1]["event_id"]
                    )
                    self.assertEqual(received[0][0], "/api/webhook/" + secret)
                    self.assertNotIn(secret, repr(reports))
        finally:
            sink.close()
            server.shutdown()
            server.server_close()
            thread.join()

    def test_bounded_alert_queue_and_latest_status(self):
        sink = HomeAssistantSink(options(), lambda *args: None)
        policy = EventPolicy(options(), ["audio"])
        for _ in range(100):
            sink.submit(policy.envelope(notify=True))
            latest = policy.envelope()
            sink.submit(latest)
        self.assertEqual(len(sink.pending), 32)
        self.assertIs(sink.latest, latest)

    def test_expired_event_is_not_sent(self):
        reported = threading.Event()
        sink = HomeAssistantSink(
            options(), lambda state, _: reported.set() if state == "expired" else None
        )
        with patch.object(sink, "send") as send:
            sink.start()
            sink.submit(
                dict(notify=True, expires_at_ns=time.time_ns() - 1, event_id="old")
            )
            self.assertTrue(reported.wait(2))
            sink.close()
            send.assert_not_called()


if __name__ == "__main__":
    unittest.main()
