from __future__ import annotations

import asyncio
import logging
import tempfile
import threading
from typing import Optional

_log = logging.getLogger(__name__)

_MAX_CONNECT_RETRIES = 3
_RETRY_BACKOFF_SEC = 5.0


_PROGRAM_SOURCE = """\
from pybricks.hubs import PrimeHub
from pybricks.pupdevices import Motor
from pybricks.parameters import Port, Stop
import usys as sys

_PORT_MAP = {
    "A": Port.A,
    "B": Port.B,
    "C": Port.C,
    "D": Port.D,
    "E": Port.E,
    "F": Port.F,
}

_motors = {}

def _get_motor(port_name):
    if port_name in _motors:
        return _motors[port_name]
    port = _PORT_MAP.get(port_name)
    if port is None:
        return None
    try:
        motor = Motor(port)
    except Exception:
        return None
    _motors[port_name] = motor
    return motor

def _ok(port_name, cmd, *args):
    print("OK", cmd, port_name, *args)

def _err(port_name, cmd, exc):
    print("ERR", cmd, port_name, repr(exc))

def _dispatch(parts):
    cmd = parts[0]
    port_name = parts[1]
    motor = _get_motor(port_name)
    if motor is None:
        print("ERR no_motor", port_name)
        return

    if cmd == "POS":
        speed = float(parts[2])
        angle = float(parts[3])
        motor.run_target(int(speed), angle, then=Stop.HOLD, wait=False)
        _ok(port_name, cmd, angle)

    elif cmd == "SET":
        angle = float(parts[2])
        motor.run_target(500, angle, then=Stop.HOLD, wait=False)
        _ok(port_name, cmd, angle)

    elif cmd == "SPD":
        speed = float(parts[2])
        motor.run(int(speed))
        _ok(port_name, cmd, speed)

    elif cmd == "DC":
        duty = float(parts[2])
        motor.dc(int(duty))
        _ok(port_name, cmd, duty)

    elif cmd == "STOP":
        motor.stop()
        _ok(port_name, cmd)

    elif cmd == "BRAKE":
        motor.brake()
        _ok(port_name, cmd)

    elif cmd == "HOLD":
        motor.hold()
        _ok(port_name, cmd)

    elif cmd == "RST":
        angle = float(parts[2]) if len(parts) > 2 else 0
        motor.reset_angle(int(angle))
        _ok(port_name, cmd, angle)

    elif cmd == "RTIME":
        speed = float(parts[2])
        time_ms = float(parts[3])
        motor.run_time(int(speed), int(time_ms), then=Stop.HOLD, wait=False)
        _ok(port_name, cmd, speed, time_ms)

    elif cmd == "RANG":
        speed = float(parts[2])
        angle = float(parts[3])
        motor.run_angle(int(speed), int(angle), then=Stop.HOLD, wait=False)
        _ok(port_name, cmd, speed, angle)

    else:
        print("ERR unknown_cmd", cmd)

print("READY")
while True:
    line = sys.stdin.readline()
    if not line:
        continue
    parts = line.strip().split()
    if len(parts) < 2:
        continue
    try:
        _dispatch(parts)
    except Exception as exc:
        _err(parts[1] if len(parts) > 1 else "?", parts[0], exc)
"""


class _AsyncRunner:
    def __init__(self) -> None:
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever, daemon=True)
        self._thread.start()

    def run(self, coro):
        future = asyncio.run_coroutine_threadsafe(coro, self._loop)
        return future.result()

    def close(self) -> None:
        self._loop.call_soon_threadsafe(self._loop.stop)
        self._thread.join(timeout=2)


class PybricksBleTransport:
    _shared = {}

    @classmethod
    def get_shared(cls, hub_id: Optional[str]) -> "PybricksBleTransport":
        key = hub_id or ""
        if key not in cls._shared:
            cls._shared[key] = {"transport": cls(), "refcount": 0}
        cls._shared[key]["refcount"] += 1
        return cls._shared[key]["transport"]

    @classmethod
    def release_shared(cls, hub_id: Optional[str]) -> None:
        key = hub_id or ""
        entry = cls._shared.get(key)
        if entry is None:
            return
        entry["refcount"] -= 1
        if entry["refcount"] <= 0:
            entry["transport"].disconnect(hub_id)
            cls._shared.pop(key, None)

    def __init__(self) -> None:
        self._runner = _AsyncRunner()
        self._hub = None
        self._hub_id: Optional[str] = None
        self._program_path: Optional[str] = None

    def connect(self, hub_id: Optional[str]) -> None:
        if self._hub is not None:
            if self._hub_id != hub_id:
                raise RuntimeError("Transport already connected to a different hub_id")
            return
        self._hub_id = hub_id
        self._hub = self._runner.run(self._connect_async(hub_id))

    def disconnect(self, hub_id: Optional[str]) -> None:
        if self._hub is None:
            return
        if self._hub_id != hub_id:
            raise RuntimeError("Transport hub_id mismatch during disconnect")
        try:
            self._runner.run(self._hub.disconnect())
        except Exception as exc:
            _log.warning("BLE disconnect error (ignored): %s", exc)
        self._hub = None
        self._hub_id = None
        self._runner.close()

    def _send(self, hub_id: Optional[str], line: str) -> None:
        if self._hub is None:
            raise RuntimeError("Transport is not connected")
        if self._hub_id != hub_id:
            raise RuntimeError("Transport hub_id mismatch during command")
        self._runner.run(self._hub.write_line(line))

    def set_motor_angle(self, hub_id: Optional[str], port: str, angle: float) -> None:
        self._send(hub_id, f"SET {port} {angle}")

    def run_target(
        self, hub_id: Optional[str], port: str, speed: float, angle: float
    ) -> None:
        self._send(hub_id, f"POS {port} {speed} {angle}")

    def run_speed(self, hub_id: Optional[str], port: str, speed: float) -> None:
        self._send(hub_id, f"SPD {port} {speed}")

    def set_dc(self, hub_id: Optional[str], port: str, duty: float) -> None:
        self._send(hub_id, f"DC {port} {duty}")

    def stop(self, hub_id: Optional[str], port: str) -> None:
        self._send(hub_id, f"STOP {port}")

    def brake(self, hub_id: Optional[str], port: str) -> None:
        self._send(hub_id, f"BRAKE {port}")

    def hold(self, hub_id: Optional[str], port: str) -> None:
        self._send(hub_id, f"HOLD {port}")

    def reset_angle(
        self, hub_id: Optional[str], port: str, angle: float = 0
    ) -> None:
        self._send(hub_id, f"RST {port} {angle}")

    def run_time(
        self, hub_id: Optional[str], port: str, speed: float, time_ms: float
    ) -> None:
        self._send(hub_id, f"RTIME {port} {speed} {time_ms}")

    def run_angle(
        self, hub_id: Optional[str], port: str, speed: float, angle: float
    ) -> None:
        self._send(hub_id, f"RANG {port} {speed} {angle}")

    @staticmethod
    async def _remove_stale_ble_device(address: str) -> None:
        """Try to remove a cached BLE device so the adapter re-scans cleanly."""
        try:
            proc = await asyncio.create_subprocess_exec(
                "bluetoothctl", "remove", address,
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.DEVNULL,
            )
            await asyncio.wait_for(proc.wait(), timeout=5)
        except Exception:
            pass

    async def _connect_async(self, hub_id: Optional[str]):
        try:
            from pybricksdev.ble import find_device
            from pybricksdev.connections.pybricks import PybricksHubBLE
        except Exception as exc:
            raise RuntimeError("pybricksdev BLE dependencies are not available") from exc

        last_exc: Exception | None = None
        last_device_address: str | None = None

        for attempt in range(1, _MAX_CONNECT_RETRIES + 1):
            hub = None
            try:
                _log.info(
                    "BLE connect attempt %d/%d for hub '%s'",
                    attempt, _MAX_CONNECT_RETRIES, hub_id,
                )

                if attempt > 1 and last_device_address:
                    _log.info(
                        "Removing stale BLE cache for %s", last_device_address,
                    )
                    await self._remove_stale_ble_device(last_device_address)

                _log.info("Scanning for hub '%s' (timeout=15s)...", hub_id)
                device = await find_device(name=hub_id, timeout=15)
                last_device_address = device.address
                _log.info("Found hub '%s' at %s", hub_id, device.address)

                hub = PybricksHubBLE(device)
                await hub.connect()
                _log.info("BLE connected to hub '%s'", hub_id)

                await hub.run(
                    self._ensure_program_path(),
                    wait=False,
                    print_output=True,
                    line_handler=True,
                )
                await asyncio.sleep(0.5)
                _log.info(
                    "Program uploaded to hub '%s' on attempt %d",
                    hub_id, attempt,
                )
                return hub
            except Exception as exc:
                last_exc = exc
                _log.warning(
                    "BLE connect attempt %d/%d failed for hub '%s': %s",
                    attempt, _MAX_CONNECT_RETRIES, hub_id, exc,
                )
                if hub is not None:
                    try:
                        await hub.disconnect()
                    except Exception:
                        pass
                if attempt < _MAX_CONNECT_RETRIES:
                    wait = _RETRY_BACKOFF_SEC * attempt
                    _log.info("Retrying in %.1fs...", wait)
                    await asyncio.sleep(wait)

        raise RuntimeError(
            f"Failed to connect to hub '{hub_id}' after "
            f"{_MAX_CONNECT_RETRIES} attempts"
        ) from last_exc

    def _ensure_program_path(self) -> str:
        if self._program_path is None:
            handle = tempfile.NamedTemporaryFile(
                mode="w", prefix="pybricks_driver_", suffix=".py", delete=False
            )
            handle.write(_PROGRAM_SOURCE)
            handle.flush()
            self._program_path = handle.name
            handle.close()
        return self._program_path
