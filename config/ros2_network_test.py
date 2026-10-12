import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from config.proto.config_pb2 import Ros2NetworkConfig
from config.ros2_network import _helper_path, network_environment


class NetworkEnvironmentTest(unittest.TestCase):
    def test_inherit_preserves_environment(self):
        with patch.dict(os.environ, {"RMW_IMPLEMENTATION": "other"}, clear=True):
            before = dict(os.environ)
            with network_environment(Ros2NetworkConfig()):
                self.assertEqual(dict(os.environ), before)

    def test_reuses_parent_profile_and_cleans_up_on_exception(self):
        config = Ros2NetworkConfig(
            discovery=Ros2NetworkConfig.STATIC_PEERS,
            peers=["192.0.2.2"],
            domain_id=17,
        )
        with patch.dict(os.environ, {"ROS_DOMAIN_ID": "4"}, clear=True):
            with self.assertRaisesRegex(ValueError, "test failure"):
                with network_environment(config):
                    path = Path(os.environ["FASTRTPS_DEFAULT_PROFILES_FILE"])
                    self.assertTrue(path.is_file())
                    self.assertEqual(os.environ["ROS_DOMAIN_ID"], "17")
                    with network_environment(config):
                        self.assertEqual(
                            Path(os.environ["FASTRTPS_DEFAULT_PROFILES_FILE"]), path
                        )
                    self.assertTrue(path.is_file())
                    raise ValueError("test failure")
            self.assertFalse(path.exists())
            self.assertEqual(dict(os.environ), {"ROS_DOMAIN_ID": "4"})

    def test_invalid_config_does_not_mutate_environment(self):
        with patch.dict(os.environ, {}, clear=True):
            config = Ros2NetworkConfig(discovery=Ros2NetworkConfig.STATIC_PEERS)
            with self.assertRaisesRegex(RuntimeError, "at least one peer"):
                with network_environment(config):
                    self.fail("Invalid config was accepted")
            self.assertEqual(dict(os.environ), {})

    def test_export_persists_independent_profile_and_quotes_shell_paths(self):
        config = Ros2NetworkConfig(
            discovery=Ros2NetworkConfig.STATIC_PEERS, peers=["192.0.2.2"]
        )
        with tempfile.TemporaryDirectory(prefix="network ' export ") as directory:
            preset = Path(directory) / "network.pbtxt"
            preset.write_text(
                "general { ros2_network { discovery: STATIC_PEERS "
                'peers: "192.0.2.2" } }'
            )
            with patch.dict(os.environ, {}, clear=True):
                with network_environment(config):
                    parent_profile = Path(os.environ["FASTRTPS_DEFAULT_PROFILES_FILE"])
                    exported = subprocess.run(
                        [
                            str(_helper_path()),
                            f"--config={preset}",
                            f"--output_parent={directory}",
                        ],
                        check=True,
                        capture_output=True,
                        text=True,
                    )
                self.assertFalse(parent_profile.exists())
                shell = subprocess.run(
                    ["/bin/sh"],
                    input=exported.stdout
                    + '\nprintf "%s" "$FASTRTPS_DEFAULT_PROFILES_FILE"\n',
                    capture_output=True,
                    text=True,
                    check=True,
                )
                profile = Path(shell.stdout)
                self.assertTrue(profile.is_file())
                self.assertNotEqual(profile, parent_profile)


if __name__ == "__main__":
    unittest.main()
