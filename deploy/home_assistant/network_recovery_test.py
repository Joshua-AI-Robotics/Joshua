import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from network_recovery import decide, observe, recover, save_state

HEALTHY = dict(
    local_ok=True, internet_ok=True, tailscale_ok=True, tailscale_state="Running"
)
DISCONNECTED = dict(HEALTHY, local_ok=False, internet_ok=False, tailscale_ok=False)


class RecoveryTest(unittest.TestCase):
    def test_probe_distinguishes_gateway_access_from_internet_failure(self):
        routes = [{"dev": "wlan0", "gateway": "192.0.2.1"}]
        tail = {"BackendState": "Running", "Self": {"Online": True}}
        for internet, ping, neighbor, expected in [
            (True, False, [], (True, True)),
            (False, True, [], (True, False)),
            (False, False, [{"state": ["REACHABLE"]}], (True, False)),
            (False, False, [{"state": ["FAILED"]}], (False, False)),
        ]:

            def fake_command(*args):
                if args[:4] == ("ip", "-j", "-4", "route"):
                    return True, json.dumps(routes)
                if args[0] == "ping":
                    return ping, ""
                if args[:3] == ("ip", "-j", "neigh"):
                    return True, json.dumps(neighbor)
                if args[0] == "tailscale":
                    return True, json.dumps(tail)
                self.fail(f"Unexpected probe: {args}")

            with patch("network_recovery.command", side_effect=fake_command), patch(
                "network_recovery.internet_probe", return_value=internet
            ):
                health = observe("wlan0")
                self.assertEqual((health["local_ok"], health["internet_ok"]), expected)

    def step(self, state, health, uptime, now=None, boot="boot1"):
        return decide(
            state, health, now if now is not None else 10000 + uptime, uptime, boot
        )

    def test_boot_grace_and_short_disconnect(self):
        state, action = self.step({}, DISCONNECTED, 179)
        self.assertEqual(action, "boot_grace")
        state, action = self.step(state, DISCONNECTED, 180)
        self.assertEqual(action, "local_network_down")
        _, action = self.step(state, DISCONNECTED, 299)
        self.assertEqual(action, "local_network_down")

    def test_reconnect_before_one_graceful_reboot(self):
        state, _ = self.step({}, DISCONNECTED, 180)
        state, action = self.step(state, DISCONNECTED, 300)
        self.assertEqual(action, "reconnect")
        state, action = self.step(state, DISCONNECTED, 480)
        self.assertEqual(action, "reboot")
        state, _ = self.step(state, DISCONNECTED, 180, now=11000, boot="boot2")
        for uptime in [300, 480, 900, 5000, 10000]:
            state, action = self.step(
                state, DISCONNECTED, uptime, now=11000 + uptime, boot="boot2"
            )
            self.assertNotEqual(action, "reboot")

    def test_reboot_limit_persists_after_recovery_and_boot(self):
        state = dict(boot_id="boot1", last_reboot=10000, rebooted_outage=True)
        state, _ = self.step(state, HEALTHY, 200, now=10200, boot="boot2")
        state, _ = self.step(state, HEALTHY, 500, now=10500, boot="boot2")
        self.assertFalse(state["rebooted_outage"])
        state, _ = self.step(state, DISCONNECTED, 600, now=10600, boot="boot2")
        state, _ = self.step(state, DISCONNECTED, 720, now=10720, boot="boot2")
        state, action = self.step(state, DISCONNECTED, 900, now=10900, boot="boot2")
        self.assertNotEqual(action, "reboot")
        _, action = self.step(state, DISCONNECTED, 3600, now=13600, boot="boot2")
        self.assertEqual(action, "reboot")

    def test_clock_rollback_cannot_bypass_reboot_limit(self):
        state = dict(
            boot_id="boot1", last_reboot=10000, local_failed_since=180, reconnected=True
        )
        _, action = self.step(state, DISCONNECTED, 500, now=5000)
        self.assertNotEqual(action, "reboot")

    def test_router_reachable_but_internet_down_never_reboots(self):
        health = dict(HEALTHY, internet_ok=False, tailscale_ok=False)
        state = {}
        for uptime in [180, 500, 1000, 10000]:
            state, action = self.step(state, health, uptime)
            self.assertEqual(action, "upstream_unreachable")

    def test_tailscale_failure_restarts_only_tailscale_with_cooldown(self):
        health = dict(HEALTHY, tailscale_ok=False)
        state, _ = self.step({}, health, 180)
        state, action = self.step(state, health, 300)
        self.assertEqual(action, "restart_tailscale")
        state, action = self.step(state, health, 420)
        self.assertEqual(action, "tailscale_unavailable")
        _, action = self.step(state, health, 900)
        self.assertEqual(action, "restart_tailscale")

    def test_intentionally_stopped_or_unauthenticated_tailscale(self):
        for backend in ["Stopped", "NeedsLogin", "NeedsMachineAuth"]:
            _, action = self.step(
                {}, dict(HEALTHY, tailscale_ok=False, tailscale_state=backend), 10000
            )
            self.assertEqual(action, "tailscale_needs_attention")

    def test_recovery_clears_failure_window(self):
        state, _ = self.step({}, DISCONNECTED, 180)
        state, _ = self.step(state, HEALTHY, 280)
        state, action = self.step(state, DISCONNECTED, 480)
        self.assertEqual(action, "local_network_down")
        self.assertEqual(state["local_failed_since"], 480)

    def test_state_round_trip_and_graceful_reboot(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            state = dict(last_reboot=12345, rebooted_outage=True)
            save_state(path, state)
            self.assertEqual(json.loads(path.read_text()), state)
        with patch("network_recovery.command", return_value=(True, "")) as run:
            self.assertTrue(recover("reboot", "wlan0"))
            run.assert_called_once_with("systemctl", "reboot")

    def test_wifi_recovery_uses_network_owner(self):
        with patch("network_recovery.Path.exists", return_value=True), patch(
            "network_recovery.command", return_value=(True, "")
        ) as run:
            self.assertTrue(recover("reconnect", "wlan0"))
            self.assertEqual(
                [call.args for call in run.call_args_list],
                [
                    ("systemctl", "restart", "netplan-wpa-wlan0.service"),
                    ("networkctl", "reconfigure", "wlan0"),
                    ("networkctl", "renew", "wlan0"),
                ],
            )


if __name__ == "__main__":
    unittest.main()
