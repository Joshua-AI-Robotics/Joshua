import os
import subprocess
import tempfile
import unittest
from pathlib import Path

BINARY = (
    Path(os.environ["TEST_SRCDIR"])
    / os.environ["TEST_WORKSPACE"]
    / "launcher/joshua_main"
)


class LauncherCliTest(unittest.TestCase):
    def run_launcher(self, *args):
        return subprocess.run(
            [str(BINARY), *args], capture_output=True, text=True, timeout=10
        )

    def test_missing_or_empty_config_is_usage_error(self):
        for args in ((), ("--config=",), ("--config", "")):
            with self.subTest(args=args):
                result = self.run_launcher(*args)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn("--config PATH is required", result.stderr)
                self.assertIn("Usage:", result.stderr)
                self.assertNotIn("Starting Joshua", result.stderr)

    def test_explicit_config_reaches_config_loading(self):
        # Malformed input proves argument forwarding without constructing hardware.
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "invalid config.pbtxt"
            path.write_text("not_a_config_field: true\n")
            for args in (("--config", str(path)), (f"--config={path}",)):
                with self.subTest(args=args):
                    result = self.run_launcher(*args)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("not_a_config_field", result.stderr)
                    self.assertNotIn("--config PATH is required", result.stderr)
                    self.assertNotIn("Starting Joshua", result.stderr)

    def test_help_and_version_do_not_require_config(self):
        result = self.run_launcher("--help")
        # gflags returns 1 for help.
        self.assertEqual(result.returncode, 1)
        self.assertIn("--config PATH", result.stdout)
        self.assertNotIn("ERROR: --config", result.stderr)
        result = self.run_launcher("--version")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("joshua_main", result.stdout)


if __name__ == "__main__":
    unittest.main()
