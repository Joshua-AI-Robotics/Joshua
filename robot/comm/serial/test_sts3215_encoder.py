#!/usr/bin/env python3
from __future__ import annotations

import os
import sys

import argparse
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from robot.comm.serial.serial import Serial


def calculate_checksum(payload: bytes) -> int:
    return (~sum(payload) & 0xFF)


def create_read_position_packet(servo_id: int) -> bytes:
    packet = bytearray(
        [
            0xFF,
            0xFF,
            servo_id & 0xFF,
            0x04,  # length
            0x02,  # READ_DATA
            0x38,  # PRESENT_POSITION_L
            0x02,  # length to read
        ]
    )
    packet.append(calculate_checksum(packet[2:]))
    return bytes(packet)


def parse_position_response(response: bytes) -> int:
    if len(response) != 8:
        raise ValueError(f"Unexpected response length: {len(response)}")
    if response[0] != 0xFF or response[1] != 0xFF:
        raise ValueError("Invalid header in response")
    if response[4] != 0:
        raise ValueError(f"Servo returned error byte: {response[4]}")
    expected_checksum = calculate_checksum(response[2:7])
    if response[7] != expected_checksum:
        raise ValueError("Checksum mismatch")
    return (response[6] << 8) | response[5]


def main() -> int:
    parser = argparse.ArgumentParser(description="STS3215 encoder read test")
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial port path")
    parser.add_argument("--baudrate", type=int, default=1000000, help="Serial baudrate")
    parser.add_argument("--servo-id", type=int, default=1, help="Servo ID")
    parser.add_argument("--interval", type=float, default=0.1, help="Seconds between reads")
    args = parser.parse_args()

    serial = Serial(args.port, args.baudrate)

    try:
        packet = create_read_position_packet(args.servo_id)
        response = serial.atomic_read(packet, 8)
        position = parse_position_response(response)
        print(f"position={position}")
    except Exception as exc:
        print(f"error={exc}")

    serial.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
