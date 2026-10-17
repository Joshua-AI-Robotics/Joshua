"""Deployment tests: privacy defaults, preset endpoint, and overwrite protection."""

import json
import stat
import tempfile
import unittest
from pathlib import Path

from config.proto.config_pb2 import Config

from ros2.utils.home_assistant_setup import render


class SetupTest(unittest.TestCase):
    def test_render_preserves_endpoint_and_credentials(self):
        config = Config()
        stream = config.utilities.media_streams.add()
        stream.stream_name = "nursery"
        stream.rtsp_port = 28554
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "ha"
            render(config, output, "100.87.146.64")
            secret = (output / "webhook.secret").read_text().strip()
            self.assertEqual(len(secret), 64)
            self.assertIn(secret, (output / "config/secrets.yaml").read_text())
            self.assertEqual(
                stat.S_IMODE((output / "webhook.secret").stat().st_mode), 0o600
            )
            self.assertIn(
                "rtsp://127.0.0.1:28554/nursery", (output / "README.md").read_text()
            )
            relay = json.loads((output / "go2rtc.yaml").read_text())
            self.assertEqual(relay["webrtc"]["listen"], "100.87.146.64:18555")
            self.assertEqual(relay["api"]["listen"], "127.0.0.1:1984")
            http = json.loads((output / "config/.storage/http").read_text())["data"]
            self.assertIsNone(http["pending"])
            self.assertEqual(http["stable"]["server_host"], ["127.0.0.1"])
            self.assertTrue((output / "install_services.sh").is_file())
            self.assertTrue((output / "run_monitor.sh").is_file())
            with self.assertRaises(FileExistsError):
                render(config, output, "100.87.146.64")
            self.assertEqual(secret, (output / "webhook.secret").read_text().strip())

    def test_invalid_ip_and_conflicting_port(self):
        config = Config()
        stream = config.utilities.media_streams.add()
        stream.stream_name = "baby"
        stream.rtsp_port = 8123
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "ha"
            with self.assertRaises(ValueError):
                render(config, output, "192.168.1.20")
            with self.assertRaises(ValueError):
                render(config, output, "100.87.146.64")
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
