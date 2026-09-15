"""Private executor client. Hardware policy lives in C++, not this adapter."""

import json
import subprocess
import threading


class ExecutorClient:
    def __init__(self, executable, config, hardware_confirmed=False):
        command = [executable, config]
        if hardware_confirmed:
            command.append("--hardware-and-reference-confirmed")
        self._process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True
        )
        self._lock = threading.Lock()

    def request(self, operation, **arguments):
        # Serialize pipe access only; motion is monitored by the executor and
        # write_position returns immediately after acceptance. Never replay.
        with self._lock:
            if self._process.poll() is not None:
                raise RuntimeError("Executor exited; operator restart required")
            payload = json.dumps({"operation": operation, **arguments}, allow_nan=False)
            try:
                self._process.stdin.write(payload + "\n")
                self._process.stdin.flush()
                line = self._process.stdout.readline()
            except (BrokenPipeError, OSError) as exc:
                raise RuntimeError(
                    "Executor connection lost; do not retry motion"
                ) from exc
            if not line:
                raise RuntimeError("Executor connection lost; do not retry motion")
            response = json.loads(line)
            if "error" in response:
                raise RuntimeError(response["error"])
            return response

    def close(self):
        with self._lock:
            self._process.stdin.close()  # EOF requests orderly disable/shutdown.
            try:
                self._process.wait(timeout=5)
            except subprocess.TimeoutExpired as exc:
                # Killing a wedged executor cannot establish that hardware stopped.
                raise RuntimeError(
                    "Executor shutdown unconfirmed; check hardware"
                ) from exc
            finally:
                self._process.stdout.close()
