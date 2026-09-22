"""Hardware-free real-process IPC/config integration."""

import sys
import unittest

from client import RosBridgeClient

EXECUTOR, CONFIG = sys.argv[1:3]
del sys.argv[1:3]


class ExecutorTest(unittest.TestCase):
    def test_offline_discovery_and_rejected_motion(self):
        client = RosBridgeClient(EXECUTOR, CONFIG)
        try:
            devices = client.request("list_devices")["devices"]
            self.assertEqual(len(devices), 1)
            device_id = devices[0]["device_id"]
            description = client.request("describe_device", device_id=device_id)
            self.assertEqual(description["unit"], "degrees")
            self.assertFalse(description["connected"])
            with self.assertRaisesRegex(RuntimeError, "offline"):
                client.request(
                    "write_position", device_id=device_id, position_degrees=10
                )
            with self.assertRaisesRegex(RuntimeError, "Unknown device"):
                client.request("describe_device", device_id="unknown")
            self.assertEqual(len(client.request("list_devices")["devices"]), 1)
        finally:
            client.close()


if __name__ == "__main__":
    unittest.main()
