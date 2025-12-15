"""
Simple Pybricks program to bridge ROS commands to a Spike Prime motor.

Protocol (line-based over stdin/stdout):
- SET <PORT> <VALUE>: sets motor duty cycle on the given port (A-F), VALUE in [-1.0, 1.0].
  Replies: OK or ERR
- GET <PORT>: reads motor angle in degrees from the given port.
  Replies: <float> or ERR

Run this on the hub via the Pybricks app or pybricksdev, then start the Python bridge
with PYTHON_BRIDGE_BACKEND=pybricks PYBRICKS_TRANSPORT=usb and publish Float32 commands
to the configured topic (e.g., spike/motor_A/command).
"""

from pybricks.hubs import PrimeHub
from pybricks.pupdevices import Motor
from pybricks.parameters import Color, Port

# Micropython on the hub may expose sys as usys; try both.
try:
    import sys  # type: ignore
except ImportError:  # pragma: no cover
    import usys as sys  # type: ignore


hub = PrimeHub()
hub.light.on(Color.GREEN)

PORT_MAP = {
    "A": Port.A,
    "B": Port.B,
    "C": Port.C,
    "D": Port.D,
    "E": Port.E,
    "F": Port.F,
}

# Cache motor instances for connected ports to avoid recreating on each command.
MOTORS = {}
for name, port in PORT_MAP.items():
    try:
        MOTORS[name] = Motor(port)
    except Exception:
        pass


def get_motor(port_name: str) -> Motor | None:
    return MOTORS.get(port_name.upper())


def handle_set(port: str, value_str: str) -> str:
    motor = get_motor(port)
    if not motor:
        return "ERR"
    try:
        value = float(value_str)
        # Clamp to [-1.0, 1.0] duty cycle
        value = max(-1.0, min(1.0, value))
        motor.dc(int(value * 100))
        return "OK"
    except Exception:
        return "ERR"


def handle_get(port: str) -> str:
    motor = get_motor(port)
    if not motor:
        return "ERR"
    try:
        return str(float(motor.angle()))
    except Exception:
        return "ERR"


def main() -> None:
    while True:
        line = sys.stdin.readline()
        if not line:
            continue
        parts = line.strip().split()
        if not parts:
            print("ERR")
            sys.stdout.flush()
            continue
        cmd = parts[0].upper()
        if cmd == "SET" and len(parts) == 3:
            resp = handle_set(parts[1], parts[2])
        elif cmd == "GET" and len(parts) == 2:
            resp = handle_get(parts[1])
        else:
            resp = "ERR"
        print(resp)
        sys.stdout.flush()


if __name__ == "__main__":
    main()
