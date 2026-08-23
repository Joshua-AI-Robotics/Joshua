from __future__ import annotations

import importlib
import os
import threading
import time

# TODO(hmoon): This file stays alive only for
# robot/comm/serial/test_sts3215_encoder.py (perception-adjacent). It is
# otherwise dead on the action/comm side after comm_factory.py's removal
# (docs/BOARD_LAYER_RFC.md §10 Phase 9). Retire it once RFC §10 Phase 6
# (perception layer parity) removes that dependency too.


class Serial:
    """Thin wrapper around pyserial to mirror the C++ Serial interface."""

    # Timeout based on 30Hz polling rate.
    def __init__(self, port: str, baudrate: int, timeout: float = 0.03) -> None:
        pyserial = _import_pyserial()

        if not port:
            raise ValueError("Serial port must be provided")
        if baudrate <= 0:
            raise ValueError("Serial baudrate must be positive")

        self._serial = pyserial.Serial(port, baudrate, timeout=timeout)
        if hasattr(self._serial, "write_timeout"):
            self._serial.write_timeout = timeout
        self._port = port
        self._baudrate = baudrate
        self._timeout = timeout
        self._lock = threading.Lock()

    @property
    def port(self) -> str:
        return self._port

    @property
    def baudrate(self) -> int:
        return self._baudrate

    @property
    def is_open(self) -> bool:
        return bool(self._serial.is_open)

    def open(self) -> None:
        if not self.is_open:
            raise RuntimeError("Serial port not open.")

    def write(self, data: bytes) -> int:
        with self._lock:
            if not self.is_open:
                raise RuntimeError("Serial port not open for writing.")
            return self._serial.write(data)

    def read(self, size: int) -> bytes:
        with self._lock:
            if not self.is_open:
                raise RuntimeError("Serial port not open for reading.")
            return self._read_exact(size, self._timeout)

    def atomic_read(self, command: bytes, expected_response_size: int) -> bytes:
        with self._lock:
            if not self.is_open:
                raise RuntimeError("Serial port not open for query.")
            self.flush()
            self._serial.write(command)
            return self._read_exact(expected_response_size, 0.02)

    def close(self) -> None:
        with self._lock:
            self._serial.close()

    def flush(self) -> None:
        if not self.is_open:
            raise RuntimeError("Serial port not open for flushing.")
        self._serial.reset_input_buffer()

    def _read_exact(self, size: int, timeout: float) -> bytes:
        if size <= 0:
            return b""

        deadline = time.monotonic() + max(0.0, timeout)
        chunks: list[bytes] = []
        bytes_read = 0
        original_timeout = self._serial.timeout
        try:
            while bytes_read < size:
                remaining = size - bytes_read
                now = time.monotonic()
                time_left = deadline - now
                if time_left <= 0:
                    break
                self._serial.timeout = time_left
                chunk = self._serial.read(remaining)
                if not chunk:
                    break
                chunks.append(chunk)
                bytes_read += len(chunk)
        finally:
            self._serial.timeout = original_timeout

        data = b"".join(chunks)
        if len(data) != size:
            raise TimeoutError(
                f"Serial read timed out: got {len(data)} bytes, expected {size}"
            )
        return data


def _import_pyserial():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

    def _load_serial() -> object:
        try:
            return importlib.import_module("serial")
        except ImportError as exc:
            raise RuntimeError("pyserial is required for Serial comms") from exc

    module = _load_serial()
    module_file = getattr(module, "__file__", "")
    if module_file and os.path.samefile(module_file, __file__):
        if "serial" in os.sys.modules:
            os.sys.modules.pop("serial", None)

        original_sys_path = list(os.sys.path)
        try:
            os.sys.path = [
                path
                for path in original_sys_path
                if not os.path.abspath(path).startswith(repo_root)
            ]
            module = _load_serial()
            module_file = getattr(module, "__file__", "")
        finally:
            os.sys.path = original_sys_path

    if module_file and os.path.samefile(module_file, __file__):
        raise RuntimeError(
            "pyserial is required for Serial comms, but the import was shadowed by "
            "robot/comm/serial/serial.py. Ensure pyserial is installed and PYTHONPATH "
            "does not shadow it."
        )

    return module
