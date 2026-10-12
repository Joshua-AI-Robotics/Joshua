"""Hardware-free discovery test using two independently initialized processes."""

import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from ros2.utils.ros_python_paths import setup_import_paths

setup_import_paths()

from rclpy.node import Node  # noqa: E402
from std_msgs.msg import String  # noqa: E402

from ros2.node_runner import run_node  # noqa: E402


class ProbeNode(Node):
    def __init__(self, name, node_id, config):
        super().__init__(name)
        if self.context.get_domain_id() != config.general.ros2_network.domain_id:
            raise RuntimeError("Network config was applied after ROS initialization")
        self.started = time.monotonic()
        self.received = None
        self.publisher = self.create_publisher(String, "/joshua_network_probe", 10)
        self.subscription = self.create_subscription(
            String, "/joshua_network_probe", self.receive, 10
        )
        self.timer = self.create_timer(0.1, self.tick)

    def receive(self, message):
        if message.data != self.get_name() and self.received is None:
            self.received = time.monotonic()
            print(f"RECEIVED {message.data}", flush=True)

    def tick(self):
        message = String()
        message.data = self.get_name()
        self.publisher.publish(message)
        if self.received and time.monotonic() - self.received > 2:
            raise KeyboardInterrupt
        if time.monotonic() - self.started > 15:
            raise RuntimeError("Peer discovery timed out")


class NetworkIntegrationTest(unittest.TestCase):
    def test_two_processes_discover_using_generated_unicast_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "network.pbtxt"
            config.write_text(
                "general { ros2_network { domain_id: 17 discovery: STATIC_PEERS "
                'peers: "127.0.0.1" participant_scan_count: 32 } }'
            )
            env = {
                key: value
                for key, value in os.environ.items()
                if not key.startswith(("ROS_", "RMW_", "FASTDDS_", "FASTRTPS_"))
            }
            env.update(
                ROS_DOMAIN_ID="18", ROS_LOCALHOST_ONLY="1", ROS_LOG_DIR=directory
            )
            children = []
            try:
                for name in ("network_probe_a", "network_probe_b"):
                    children.append(
                        subprocess.Popen(
                            [
                                sys.executable,
                                __file__,
                                "--probe",
                                name,
                                "1",
                                str(config),
                            ],
                            env=env,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True,
                        )
                    )
                for child, peer in zip(
                    children, ("network_probe_b", "network_probe_a")
                ):
                    output, _ = child.communicate(timeout=25)
                    self.assertEqual(child.returncode, 0, output)
                    self.assertIn(f"RECEIVED {peer}", output)
                    self.assertNotIn("Error parsing", output)
                    self.assertNotIn("without content", output)
            finally:
                for child in children:
                    if child.poll() is None:
                        child.kill()
                    child.communicate()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--probe":
        sys.exit(run_node(ProbeNode, "network_probe", [sys.argv[0], *sys.argv[2:]]))
    unittest.main()
