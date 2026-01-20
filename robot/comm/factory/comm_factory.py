from __future__ import annotations

import threading
from typing import Dict, Tuple

from robot.comm.proto import comm_pb2
from robot.comm.serial.serial import Serial

_serial_cache: Dict[Tuple[str, int], Serial] = {}
_serial_lock = threading.Lock()


def create_serial(comm: comm_pb2.Comm) -> Serial:
    if comm.comm_type != comm_pb2.CommType.SERIAL:
        raise ValueError("Comm is not of type SERIAL")

    if not comm.HasField("serial_config"):
        raise ValueError("Comm has no serial config")

    port = comm.serial_config.port
    baudrate = int(comm.serial_config.baudrate)
    if not port:
        raise ValueError("Serial config has no port")
    if baudrate <= 0:
        raise ValueError("Serial config has no baudrate")

    key = (port, baudrate)
    with _serial_lock:
        cached = _serial_cache.get(key)
        if cached is not None and cached.is_open:
            return cached

        serial = Serial(port, baudrate)
        _serial_cache[key] = serial
        return serial
