#!/usr/bin/env python3
"""Single executable python script to control so100 arm with keyboard."""

import json
import os
import sys
import termios
import time
import tty
from collections import namedtuple

import serial

UART_PORT = '/dev/ttyACM0'
UART_BAUDRATE = 1000000

NUMBER_OF_SERVOS = 6
START_ID = 1
POSITION_STEP = 10
MOVE_SPEED = 400
MOVE_TIME = 40  # milliseconds
SERVO_COMMAND_BUFFER_TIME = 0.01  # seconds

MIN_ANGLE = 0
MAX_ANGLE = 180

# Adjust this value based on your device.
ServoLimit = namedtuple("ServoLimit", ["min", "max"])
SERVO_LIMITS = {
    0: ServoLimit(1024, 3072),
    1: ServoLimit(800, 3000),
    2: ServoLimit(950, 3000),
    3: ServoLimit(900, 3072),
    4: ServoLimit(0, 3000),
    5: ServoLimit(1762, 2400),
}

# When turning on/off, move this position to settle.
# Adjust this value based on your device.
SETUP_POSITIONS = [2070, 847, 3011, 655, 1838, 1806]
SETUP_MOVE_SPEED = 1200
SETUP_TIME = 2

current_positions = SETUP_POSITIONS[:]
saved_positions = [2048] * NUMBER_OF_SERVOS
active_servo = 0
SAVE_FILE = "servo_positions.json"


def calculate_checksum(data):
    return ~sum(data) & 0xFF


def create_move_packet(servo_id, position, speed):
    time_ms = MOVE_TIME
    packet = [
        0xFF,
        0xFF,
        servo_id,
        0x09,
        0x03,
        0x2A,
        position & 0xFF,
        (position >> 8) & 0xFF,
        time_ms & 0xFF,
        (time_ms >> 8) & 0xFF,
        speed & 0xFF,
        (speed >> 8) & 0xFF,
    ]
    packet.append(calculate_checksum(packet[2:]))
    return bytearray(packet)


def create_torque_packet(servo_id, enable):
    packet = [0xFF, 0xFF, servo_id, 0x04, 0x03, 0x28, 1 if enable else 0]
    packet.append(calculate_checksum(packet[2:]))
    return bytearray(packet)


def create_read_position_packet(servo_id):
    """Creates a packet to request the current position of a servo."""
    # The read instruction requires the starting address (0x38 for position)
    # and the number of bytes to read (2 for position).
    packet = [0xFF, 0xFF, servo_id, 0x04, 0x02, 0x38, 0x02]
    packet.append(calculate_checksum(packet[2:]))
    return bytearray(packet)


def read_servo_position(serial_obj, servo_id):
    """Sends a read request and returns the servo's current position."""
    # 1. Create and send the read packet
    read_packet = create_read_position_packet(servo_id)
    serial_obj.write(read_packet)

    # 2. Wait for and read the response
    # A status packet for a 2-byte read will be 8 bytes long
    # (Header*2, ID, Length, Error, Param*2, Checksum)
    response = serial_obj.read(8)

    # 3. Parse the response
    if len(response) == 8 and response[0] == 0xFF and response[1] == 0xFF:
        # Check for errors from the servo
        if response[4] != 0:
            # print(f"Servo {servo_id} returned an error: {response[4]}")
            return None

        # Combine the low and high bytes to get the position
        position = response[5] | (response[6] << 8)
        return position

    return 0


def getch():
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        ch = sys.stdin.read(1)
        if ch == '\x1b':
            ch += sys.stdin.read(2)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    return ch


def print_status():
    os.system('clear')
    print("╔══════════════════════════════════════════════╗")
    print("║     Feetech STS3215 Multiple Servo Control   ║")
    print("╚══════════════════════════════════════════════╝\n")

    print("┌────┬───────────┬────────┬────────┐")
    print("│ ID │ Position  │  Min   │  Max   │")
    print("├────┼───────────┼────────┼────────┤")
    for i in range(NUMBER_OF_SERVOS):
        servo_id = START_ID + i
        position = current_positions[i]
        min_pos = SERVO_LIMITS[i].min
        max_pos = SERVO_LIMITS[i].max
        note = "<- ACTIVE" if i == active_servo else ""
        print(f"│ {servo_id:2d} │  {position:7d}  │ {min_pos:6d} │ {max_pos:6d} │" + note)
    print("└────┴───────────┴────────┴────────┘\n")

    print("\nControls:")
    print("1-6: Select servo to control")
    print("← / →: Move selected servo slightly left/right")
    print("S: Save current positions")
    print("L: Load saved positions")
    print("Q or ESC: Quit")
    print("\nStatus: Active servo highlighted above\n")


def save_positions():
    try:
        with open(SAVE_FILE, 'w') as f:
            json.dump(current_positions, f)
        return True
    except Exception as e:
        print(f"Error saving positions: {e}")
        return False


def load_positions():
    global current_positions
    try:
        if os.path.exists(SAVE_FILE):
            with open(SAVE_FILE) as f:
                positions = json.load(f)
                if isinstance(positions, list) and len(positions) == NUMBER_OF_SERVOS:
                    current_positions = positions
                    return True
        return False
    except Exception as e:
        print(f"Error loading positions: {e}")
        return False


def main():
    port = input(f"Enter serial port [default: {UART_PORT}]: ").strip() or UART_PORT
    global active_servo
    try:
        print(f"Attempting to connect to {port}...")
        serial_obj = serial.Serial(port, UART_BAUDRATE, timeout=0.1)
        print(f"Connected to {port} successfully")

        print("Enabling torque on all servos...")
        for i in range(NUMBER_OF_SERVOS):
            serial_obj.write(create_torque_packet(START_ID + i, True))
            time.sleep(0.1)

        print("Read the servos angle...")
        for i in range(NUMBER_OF_SERVOS):
            servo_id = START_ID + i
            current_positions[i] = read_servo_position(serial_obj, servo_id)

        print("Moving all servos to initial position (90°)...")
        for i in range(NUMBER_OF_SERVOS):
            middle_position = (SERVO_LIMITS[i].min + SERVO_LIMITS[i].max) // 2
            current_positions[i] = middle_position
            serial_obj.write(create_move_packet(START_ID + i, middle_position, SETUP_MOVE_SPEED))
            time.sleep(SERVO_COMMAND_BUFFER_TIME)

        time.sleep(SETUP_TIME)

        print_status()

        while True:
            key = getch()
            if key.lower() == 'q':
                print("Shutting Down...")
                break
            elif key in '123456':
                active_servo = int(key) - 1
                print_status()
            elif key.lower() == 's':
                print(
                    "Positions saved successfully"
                    if save_positions()
                    else "Failed to save positions"
                )
                time.sleep(1)
                print_status()
            elif key.lower() == 'l':
                if load_positions():
                    print("Positions loaded successfully")
                    for i in range(NUMBER_OF_SERVOS):
                        serial_obj.write(
                            create_move_packet(START_ID + i, current_positions[i], MOVE_SPEED)
                        )
                        time.sleep(0.2)
                    time.sleep(1)
                else:
                    print("Failed to load positions or no saved positions found")
                time.sleep(1)
                print_status()
            elif key.startswith('\x1b['):
                arrow = key[2]
                limits = SERVO_LIMITS[active_servo]
                if arrow == 'C':
                    current_positions[active_servo] = min(
                        current_positions[active_servo] + POSITION_STEP, limits.max
                    )
                elif arrow == 'D':
                    current_positions[active_servo] = max(
                        current_positions[active_servo] - POSITION_STEP, limits.min
                    )
                serial_obj.write(
                    create_move_packet(
                        START_ID + active_servo, current_positions[active_servo], MOVE_SPEED
                    )
                )

                print_status()

    except serial.SerialException as e:
        print(f"Error: Could not open serial port {port}: {e}")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        for i in range(NUMBER_OF_SERVOS):
            serial_obj.write(create_move_packet(START_ID + i, SETUP_POSITIONS[i], SETUP_MOVE_SPEED))

        time.sleep(SETUP_TIME)

        if 'serial_obj' in locals() and serial_obj.is_open:
            try:
                print("Disabling torque on all servos...")
                for i in range(NUMBER_OF_SERVOS):
                    serial_obj.write(create_torque_packet(START_ID + i, False))
                    time.sleep(0.1)
                serial_obj.close()
                print("Serial connection closed.")
            except Exception as e:
                print(f"Error while closing connection: {e}")


if __name__ == "__main__":
    main()
