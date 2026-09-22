"""Manage one configured ROS actuator session without implementing motor I/O."""

import fcntl
import hashlib
import os
import signal
import subprocess
import tempfile
import threading
import time
from pathlib import Path

from client import RosBridgeClient


class SessionManager:
    def __init__(self, bridge, config, actuator_node, lock_dir):
        self._bridge_path = str(Path(bridge).resolve())
        self._node_path = str(Path(actuator_node).resolve())
        self._lock_dir = Path(lock_dir)
        self._mutex = threading.RLock()
        self._work = tempfile.TemporaryDirectory(prefix="joshua-session-")
        # Discovery and the launched node must use exactly the same config.
        self._config = str(Path(self._work.name) / "session.pbtxt")
        Path(self._config).write_bytes(Path(config).read_bytes())
        self._offline = RosBridgeClient(self._bridge_path, self._config)
        self._live = None
        self._node = None
        self._lease = None
        self._log = None
        self._status = "offline"
        self._closed = False

    def request(self, operation, **arguments):
        with self._mutex:
            if self._closed:
                raise RuntimeError("MCP session manager is closed")
            if self._node is not None and self._node.poll() is not None:
                self._status = "fault"
            if self._status == "active":
                return self._live.request(operation, **arguments)
            if operation in ("list_devices", "describe_device"):
                result = self._offline.request(operation, **arguments)
                result["session_status"] = self._status
                return result
            raise RuntimeError(
                "Actuator session is offline or faulted; use start_session after "
                "operator confirmation (end_session first after a fault)"
            )

    def start_session(self, hardware_ready, reference_confirmed):
        with self._mutex:
            if self._closed:
                raise RuntimeError("MCP session manager is closed")
            if hardware_ready is not True or reference_confirmed is not True:
                raise RuntimeError(
                    "Explicit operator confirmation of hardware readiness and "
                    "the coordinate reference is required; do not infer confirmation"
                )
            if self._node is not None and self._node.poll() is not None:
                self._status = "fault"
            if self._status == "active":
                state = self._live.request("read_state", device_id=self._device_id)
                if not state.get("reference_valid"):
                    raise RuntimeError(
                        "Session reference invalidated; end_session, inspect the rig, "
                        "and obtain fresh operator confirmation before starting again"
                    )
                return {**state, "session_status": "active", "already_started": True}
            if self._status != "offline":
                raise RuntimeError(
                    "Previous session failed; end_session and obtain fresh operator "
                    "confirmation before another start"
                )
            # C++ resolves and validates the preset without opening devices.
            info = self._offline.request("session_launch_info")
            self._device_id = info["device_id"]
            self._status = "starting"
            try:
                self._acquire_lease(info["serial_port"])
                self._log = open(Path(self._work.name) / "actuator.log", "w+")
                env = dict(os.environ, JOSHUA_HARDWARE_REFERENCE_CONFIRMED="1")
                self._node = subprocess.Popen(
                    [
                        self._node_path,
                        "actuator_subscriber_node_" + str(int(info["node_id"])),
                        str(int(info["node_id"])),
                        self._config,
                    ],
                    stdin=subprocess.DEVNULL,
                    stdout=self._log,
                    stderr=self._log,
                    env=env,
                    start_new_session=True,
                    # Keep ownership while the node lives, even if its manager dies.
                    pass_fds=(self._lease.fileno(),),
                )
                self._live = RosBridgeClient(self._bridge_path, self._config, True)
                deadline = time.monotonic() + 8
                while True:
                    if self._node.poll() is not None:
                        raise RuntimeError("ROS actuator node exited during startup")
                    try:
                        state = self._live.request(
                            "read_state", device_id=self._device_id
                        )
                        break
                    except RuntimeError as exc:
                        # Only discovery-before-send is retryable; no motion is sent.
                        if (
                            "ROS actuator unavailable" not in str(exc)
                            or time.monotonic() >= deadline
                        ):
                            raise
                        time.sleep(0.05)
                if not (
                    state.get("status") == "ready_disarmed"
                    and state.get("disable_acknowledged") is True
                    and state.get("reference_valid") is True
                    and state.get("feedback_valid") is True
                ):
                    raise RuntimeError(
                        "ROS startup did not establish a fresh disabled reference"
                    )
                self._status = "active"
                return {**state, "session_status": "active", "already_started": False}
            except Exception as exc:
                details = ""
                if self._log is not None:
                    self._log.flush()
                    self._log.seek(0, os.SEEK_END)
                    self._log.seek(max(0, self._log.tell() - 4000))
                    details = self._log.read()
                cleanup = self._end_locked()
                self._status = "fault"
                raise RuntimeError(
                    f"Session startup failed: {exc}; node log: {details}; "
                    f"cleanup: {cleanup}. "
                    "No position command was sent. Inspect the rig before retrying."
                ) from exc

    def _acquire_lease(self, serial_port):
        self._lock_dir.mkdir(parents=True, exist_ok=True)
        # All managed containers use the same volume and serial path. This is an
        # advisory ownership lock; standalone launchers must not run alongside it.
        key = hashlib.sha256(os.path.realpath(serial_port).encode()).hexdigest()
        lease = open(self._lock_dir / (key + ".lock"), "a+")
        try:
            fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            lease.close()
            raise RuntimeError(
                "Another managed session owns this serial device"
            ) from None
        self._lease = lease

    def end_session(self):
        with self._mutex:
            return self._end_locked()

    def _end_locked(self):
        errors = []
        acknowledged = None
        if self._live is not None:
            try:
                state = self._live.request("stop_device", device_id=self._device_id)
                acknowledged = state.get("disable_acknowledged") is True
                if not acknowledged:
                    errors.append("Driver disable unconfirmed")
            except Exception as exc:
                acknowledged = False
                errors.append(str(exc))
            try:
                self._live.close()
            except Exception as exc:
                errors.append(str(exc))
            self._live = None
        if self._node is not None:
            for sig, timeout in (
                (signal.SIGINT, 3),
                (signal.SIGTERM, 2),
                (signal.SIGKILL, 1),
            ):
                if self._node.poll() is not None:
                    break
                if sig != signal.SIGINT:
                    errors.append("ROS shutdown required " + sig.name)
                try:
                    os.killpg(self._node.pid, sig)
                except ProcessLookupError:
                    pass
                try:
                    self._node.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    continue
            if self._node.poll() is None:
                self._status = "fault"
                raise RuntimeError(
                    "ROS node shutdown unconfirmed; ownership lock retained"
                )
            self._node = None
        if self._lease is not None:
            self._lease.close()
            self._lease = None
        if self._log is not None:
            self._log.close()
            self._log = None
        self._status = "offline"
        return {
            "session_status": "offline",
            "disable_acknowledged": acknowledged,
            "physical_position_verified": False,
            "errors": errors,
        }

    def close(self):
        with self._mutex:
            if self._closed:
                return
            try:
                result = self._end_locked()
                if result["errors"]:
                    import sys

                    print("Session cleanup: " + str(result), file=sys.stderr)
            finally:
                self._offline.close()
                self._work.cleanup()
                self._closed = True
