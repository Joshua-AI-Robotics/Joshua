"""Check profile selection and generated TI configurations using fake SDK files."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from firmware.am243.prepare_flash import ARTIFACTS, prepare


class PrepareFlashTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.sdk = self.root / "sdk"
        self.images = self.root / "images"
        self.images.mkdir()
        boot = self.sdk / "mcu_plus_sdk/tools/boot"
        self.tool = boot / "uart_uniflash.py"
        for relative in (
            "uart_uniflash.py",
            "sbl_prebuilt/am243x-lp/sbl_uart_uniflash.release.hs_fs.tiimage",
            "sbl_prebuilt/am243x-lp/sbl_ospi.release.hs_fs.tiimage",
        ):
            path = boot / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("raise RuntimeError('flash tool must never run')\n")
        for profile, name in ARTIFACTS.items():
            (self.images / name).write_text(profile)
        self.output = self.root / "flash.cfg"

    def generate(self, profile="uart", operation="flashverify"):
        return prepare(
            self.sdk, self.images, profile, operation, self.output, "/dev/test-device"
        )

    def test_every_profile_selects_its_own_artifact_and_sdk_bootloader(self):
        for profile, name in ARTIFACTS.items():
            with self.subTest(profile=profile):
                image, digest, command = self.generate(profile)
                config = self.output.read_text()
                self.assertEqual(image, self.images / name)
                self.assertEqual(len(digest), 64)
                self.assertIn(f"--file={self.images / name}", config)
                self.assertIn("sbl_ospi.release.hs_fs.tiimage", config)
                self.assertIn("--flash-offset=0x0\n", config)
                self.assertIn("--flash-offset=0x80000\n", config)
                self.assertEqual(config.count("--operation=flashverify"), 2)
                self.assertNotIn("flash-phy-tuning-data", config)
                self.assertIn("/dev/test-device", command)
                self.output.unlink()

    def test_flash_includes_tuning_and_two_explicit_regions(self):
        self.generate(operation="flash")
        config = self.output.read_text()
        self.assertEqual(config.count("--operation=flash-phy-tuning-data"), 1)
        self.assertEqual(config.count("--operation=flash --flash-offset="), 2)

    def test_missing_and_empty_inputs_fail_before_creating_output(self):
        for path in (self.tool, self.images / ARTIFACTS["uart"]):
            original = path.read_bytes()
            for missing in (False, True):
                path.unlink() if missing else path.write_bytes(b"")
                with self.assertRaises(ValueError):
                    self.generate()
                self.assertFalse(self.output.exists())
                path.write_bytes(original)

    def test_existing_output_is_preserved(self):
        self.output.write_text("keep")
        with self.assertRaises(FileExistsError):
            self.generate()
        self.assertEqual(self.output.read_text(), "keep")

    def test_ambiguous_config_paths_and_invalid_ports_are_rejected(self):
        spaced = self.root / "SDK with spaces"
        self.sdk.rename(spaced)
        with self.assertRaises(ValueError):
            prepare(spaced, self.images, "uart", "flash", self.output, "/dev/test")
        self.assertFalse(self.output.exists())
        spaced.rename(self.sdk)
        for port in ("", "a\nb", "a\0b"):
            with self.assertRaises(ValueError):
                prepare(self.sdk, self.images, "uart", "flash", self.output, port)
        self.assertFalse(self.output.exists())

    def test_cli_prepares_and_prints_but_does_not_run_sdk_tool(self):
        command = [
            sys.executable,
            str(Path(__file__).with_name("prepare_flash.py")),
            "--sdk",
            str(self.sdk),
            "--image-dir",
            str(self.images),
            "--profile",
            "jw-soes",
            "--operation",
            "flashverify",
            "--output",
            str(self.output),
            "--port",
            "/dev/nonexistent-test-port",
        ]
        result = subprocess.run(command, text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("not executed", result.stdout)
        self.assertTrue(self.output.is_file())


if __name__ == "__main__":
    unittest.main()
