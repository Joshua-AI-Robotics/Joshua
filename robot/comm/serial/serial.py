from __future__ import annotations

from typing import Optional


class Serial:
    """Thin wrapper around pyserial to mirror the C++ Serial interface."""

    def __init__(self, port: str, baudrate: int, timeout: float = 0.1) -> None:
        try:
            import serial as pyserial
        except ImportError as exc:
            raise RuntimeError("pyserial is required for Serial comms") from exc

        if not port:
            raise ValueError("Serial port must be provided")
        if baudrate <= 0:
            raise ValueError("Serial baudrate must be positive")

        self._serial = pyserial.Serial(port, baudrate, timeout=timeout)
        self._port = port
        self._baudrate = baudrate

    @property
    def port(self) -> str:
        return self._port

    @property
    def baudrate(self) -> int:
        return self._baudrate

    @property
    def is_open(self) -> bool:
        return bool(self._serial.is_open)

    def write(self, data: bytes) -> int:
        return self._serial.write(data)

    def read(self, size: int) -> bytes:
        return self._serial.read(size)

    def close(self) -> None:
        self._serial.close()
