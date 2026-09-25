"""Private ROS bridge client; hardware policy lives in the ROS actuator node."""

import json
import os
import select
import subprocess
import threading
import time


class RosBridgeClient:
    def __init__(self, executable, config, connect_ros=False):
        command = [executable, config]
        if connect_ros:
            command.append("--connect-ros")
        self._process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True
        )
        self._lock = threading.Lock()
        self._pending = b""
        self._broken = False
        self._closed = False

    def request(self, operation, **arguments):
        # Serialize pipe access only; motion is monitored by the ROS actuator node and
        # write_position returns immediately after acceptance. Never replay.
        with self._lock:
            if self._closed or self._broken or self._process.poll() is not None:
                raise RuntimeError("ROS bridge exited; operator restart required")
            payload = json.dumps({"operation": operation, **arguments}, allow_nan=False)
            try:
                self._process.stdin.write(payload + "\n")
                self._process.stdin.flush()
                deadline = time.monotonic() + 12
                while b"\n" not in self._pending:
                    remaining = deadline - time.monotonic()
                    if (
                        remaining <= 0
                        or not select.select([self._process.stdout], [], [], remaining)[
                            0
                        ]
                    ):
                        self._broken = True
                        raise RuntimeError(
                            "ROS bridge response timed out; do not retry motion"
                        )
                    chunk = os.read(self._process.stdout.fileno(), 4096)
                    if not chunk:
                        self._broken = True
                        raise RuntimeError(
                            "ROS bridge connection lost; do not retry motion"
                        )
                    self._pending += chunk
                    if len(self._pending) > 65536:
                        self._broken = True
                        raise RuntimeError("ROS bridge response too large")
                line, self._pending = self._pending.split(b"\n", 1)
            except (BrokenPipeError, OSError) as exc:
                raise RuntimeError(
                    "ROS bridge connection lost; do not retry motion"
                ) from exc
            if not line:
                raise RuntimeError("ROS bridge connection lost; do not retry motion")
            response = json.loads(line)
            if "error" in response:
                raise RuntimeError(response["error"])
            return response

    def close(self):
        with self._lock:
            if self._closed:
                return
            self._closed = True
            try:
                try:
                    self._process.stdin.close()  # EOF requests ROS disable/shutdown.
                except BrokenPipeError:
                    pass
                if self._process.wait(timeout=5) != 0:
                    raise RuntimeError(
                        "ROS bridge shutdown unconfirmed; check actuator state"
                    )
            except subprocess.TimeoutExpired as exc:
                # Reap only our child. Termination is not a disable acknowledgment.
                self._process.terminate()
                try:
                    self._process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self._process.kill()
                    self._process.wait(timeout=2)
                raise RuntimeError(
                    "ROS bridge shutdown unconfirmed; check hardware"
                ) from exc
            finally:
                self._process.stdout.close()
