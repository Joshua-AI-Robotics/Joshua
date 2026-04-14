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

def _get_motor(port_name: str):
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

def _set_angle(port_name: str, angle: float):
    motor = _get_motor(port_name)
    if motor is None:
        print("ERR no_motor", port_name)
        return
    try:
        motor.run_target(500, angle, then=Stop.HOLD, wait=False)
        print("OK", port_name, angle)
    except Exception as exc:
        print("ERR set_failed", port_name, repr(exc))

print("READY")
while True:
    line = sys.stdin.readline()
    if not line:
        continue
    parts = line.strip().split()
    if len(parts) == 3 and parts[0] == "SET":
        try:
            _set_angle(parts[1], float(parts[2]))
        except Exception as exc:
            print("ERR set_failed", parts, repr(exc))
            pass
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

    def set_motor_angle(self, hub_id: Optional[str], port: str, angle: float) -> None:
        if self._hub is None:
            raise RuntimeError("Transport is not connected")
        if self._hub_id != hub_id:
            raise RuntimeError("Transport hub_id mismatch during command")
        self._runner.run(self._hub.write_line(f"SET {port} {angle}"))

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
