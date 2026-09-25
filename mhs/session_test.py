"""Hardware-free session lifecycle tests with injected processes and bridges."""

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from session import SessionManager


class FakeBridge:
    instances = []
    ready = True
    fail_start = False
    fail_disable = False

    def __init__(self, executable, config, connect_ros=False):
        self.live = connect_ros
        self.calls = []
        self.closed = False
        self.instances.append(self)

    def request(self, operation, **arguments):
        self.calls.append(operation)
        if operation == "session_launch_info":
            return {"device_id": "motor", "node_id": 7, "serial_port": "/dev/test"}
        if operation == "list_devices":
            return {"devices": [{"device_id": "motor", "connected": self.live}]}
        if operation == "read_state":
            if self.fail_start:
                raise RuntimeError("Broken bridge")
            return {
                "status": "ready_disarmed" if self.ready else "stopped",
                "reference_valid": self.ready,
                "feedback_valid": True,
                "disable_acknowledged": True,
            }
        if operation == "stop_device":
            return {"disable_acknowledged": not self.fail_disable}
        raise AssertionError(operation)

    def close(self):
        self.closed = True


class FakeNode:
    instances = []

    def __init__(self, args, **kwargs):
        self.args = args
        self.kwargs = kwargs
        self.pid = 987654
        self.returncode = None
        self.instances.append(self)

    def poll(self):
        return self.returncode

    def wait(self, timeout):
        self.returncode = 0
        return 0


class SessionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.config = Path(self.temp.name) / "config.pbtxt"
        self.config.write_text("immutable config")
        self.locks = str(Path(self.temp.name) / "locks")
        FakeBridge.instances, FakeNode.instances = [], []
        FakeBridge.ready, FakeBridge.fail_start, FakeBridge.fail_disable = (
            True,
            False,
            False,
        )
        self.bridge_patch = patch("session.RosBridgeClient", FakeBridge)
        self.node_patch = patch("session.subprocess.Popen", FakeNode)
        self.signal_patch = patch("session.os.killpg")
        for item in (self.bridge_patch, self.node_patch, self.signal_patch):
            item.start()
            self.addCleanup(item.stop)
        self.manager = SessionManager(
            "/fixed/bridge", self.config, "/fixed/node", self.locks
        )
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(self.manager.close)

    def test_discovery_never_starts_node_and_requires_both_confirmations(self):
        self.assertFalse(
            self.manager.request("list_devices")["devices"][0]["connected"]
        )
        for hardware, reference in ((False, False), (True, False), (False, True)):
            with self.assertRaisesRegex(RuntimeError, "confirmation"):
                self.manager.start_session(hardware, reference)
        self.assertEqual(FakeNode.instances, [])
        with self.assertRaisesRegex(RuntimeError, "offline"):
            self.manager.request(
                "write_position", device_id="motor", position_degrees=10
            )

    def test_start_connects_without_motion_and_uses_same_snapshot(self):
        self.config.write_text("modified after discovery")
        state = self.manager.start_session(True, True)
        self.assertEqual(state["session_status"], "active")
        node = FakeNode.instances[0]
        self.assertEqual(
            node.args[:3], ["/fixed/node", "actuator_subscriber_node_7", "7"]
        )
        self.assertEqual(Path(node.args[3]).read_text(), "immutable config")
        self.assertEqual(node.kwargs["env"]["JOSHUA_HARDWARE_REFERENCE_CONFIRMED"], "1")
        self.assertTrue(node.kwargs["pass_fds"])
        self.assertEqual(FakeBridge.instances[-1].calls, ["read_state"])
        again = self.manager.start_session(True, True)
        self.assertTrue(again["already_started"])
        self.assertEqual(len(FakeNode.instances), 1)
        ended = self.manager.end_session()
        self.assertTrue(ended["disable_acknowledged"])
        self.assertEqual(ended["errors"], [])
        self.assertEqual(node.returncode, 0)
        self.assertFalse(
            self.manager.request("list_devices")["devices"][0]["connected"]
        )

    def test_stopped_session_is_not_silently_rearmed(self):
        self.manager.start_session(True, True)
        FakeBridge.ready = False
        with self.assertRaisesRegex(RuntimeError, "fresh operator"):
            self.manager.start_session(True, True)
        self.assertEqual(len(FakeNode.instances), 1)

    def test_start_failure_cleans_up_and_requires_explicit_end(self):
        FakeBridge.fail_start = True
        with self.assertRaisesRegex(RuntimeError, "startup failed"):
            self.manager.start_session(True, True)
        self.assertEqual(FakeNode.instances[0].returncode, 0)
        self.assertTrue(FakeBridge.instances[-1].closed)
        with self.assertRaisesRegex(RuntimeError, "Previous session failed"):
            self.manager.start_session(True, True)
        self.manager.end_session()
        FakeBridge.fail_start = False
        self.assertEqual(
            self.manager.start_session(True, True)["session_status"], "active"
        )

    def test_lost_disable_ack_is_reported_even_when_process_exits(self):
        self.manager.start_session(True, True)
        FakeBridge.fail_disable = True
        result = self.manager.end_session()
        self.assertFalse(result["disable_acknowledged"])
        self.assertIn("Driver disable unconfirmed", result["errors"])
        self.assertEqual(FakeNode.instances[-1].returncode, 0)

    def test_second_manager_cannot_claim_same_device(self):
        self.manager.start_session(True, True)
        other = SessionManager("/fixed/bridge", self.config, "/fixed/node", self.locks)
        try:
            with self.assertRaisesRegex(RuntimeError, "Another managed session"):
                other.start_session(True, True)
            self.assertEqual(len(FakeNode.instances), 1)
        finally:
            other.close()

    def test_exited_node_blocks_motion_and_automatic_restart(self):
        self.manager.start_session(True, True)
        FakeNode.instances[-1].returncode = 1
        with self.assertRaisesRegex(RuntimeError, "offline or faulted"):
            self.manager.request(
                "write_position", device_id="motor", position_degrees=10
            )
        with self.assertRaisesRegex(RuntimeError, "Previous session failed"):
            self.manager.start_session(True, True)


if __name__ == "__main__":
    unittest.main()
