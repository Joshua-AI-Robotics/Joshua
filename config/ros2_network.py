"""Apply the shared C++ discovery configuration before rclpy.init()."""

import contextlib
import os
import subprocess
import tempfile
from pathlib import Path


def _helper_path() -> Path:
    # This module and helper are siblings in Bazel runfiles and packaged trees.
    # Preserve the runfiles path: resolving this module's source symlink would
    # discard the compiled helper that lives alongside it.
    helper = Path(__file__).absolute().with_name("ros2_network_export")
    if not helper.is_file():
        raise RuntimeError(f"ROS network helper missing: {helper}; rebuild the target")
    return helper


@contextlib.contextmanager
def network_environment(network):
    """Own settings/profile for this process; reuse matching inherited profiles."""
    if not network.SerializeToString():
        yield
        return
    previous = {}
    with tempfile.TemporaryDirectory(prefix="joshua-network-python-") as directory:
        result = subprocess.run(
            [
                str(_helper_path()),
                "--network_stdin",
                "--null_output",
                f"--output_parent={directory}",
            ],
            input=network.SerializeToString(),
            capture_output=True,
            check=False,
        )
        if result.returncode:
            raise RuntimeError(result.stderr.decode(errors="replace").strip())
        try:
            for entry in result.stdout.split(b"\0"):
                if not entry:
                    continue
                key, value = os.fsdecode(entry).split("=", 1)
                previous[key] = os.environ.get(key)
                os.environ[key] = value
            yield
        finally:
            for key, value in previous.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value
