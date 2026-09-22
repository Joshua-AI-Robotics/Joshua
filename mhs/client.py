"""Private ROS bridge client; hardware policy lives in the ROS actuator node."""

import json
import subprocess
import threading


class RosBridgeClient:
    def __init__(self, executable, config, connect_ros=False):
        command = [executable, config]
        if connect_ros:
            command.append("--connect-ros")
        self._process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True
        )
        self._lock = threading.Lock()

    def request(self, operation, **arguments):
        # Serialize pipe access only; motion is monitored by the ROS actuator node and
        # write_position returns immediately after acceptance. Never replay.
        with self._lock:
            if self._process.poll() is not None:
                raise RuntimeError("ROS bridge exited; operator restart required")
            payload = json.dumps({"operation": operation, **arguments}, allow_nan=False)
            try:
                self._process.stdin.write(payload + "\n")
                self._process.stdin.flush()
                line = self._process.stdout.readline()
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
            self._process.stdin.close()  # EOF requests ROS disable/shutdown.
            try:
                if self._process.wait(timeout=5) != 0:
                    raise RuntimeError(
                        "ROS bridge shutdown unconfirmed; check actuator state"
                    )
            except subprocess.TimeoutExpired as exc:
                # Killing a wedged bridge cannot establish that hardware stopped.
                raise RuntimeError(
                    "ROS bridge shutdown unconfirmed; check hardware"
                ) from exc
            finally:
                self._process.stdout.close()
