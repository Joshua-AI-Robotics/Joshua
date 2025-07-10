#!/usr/bin/env python3
""" Single executable python script to control so100 arm with xbox controller."""

import serial
import time
import termios
import tty
import sys
import os
from collections import namedtuple
import evdev
from evdev import ecodes
import select

UART_PORT = '/dev/ttyACM0'
UART_BAUDRATE = 1000000

NUMBER_OF_SERVOS = 6
START_ID = 1
POSITION_STEP = 20
MOVE_SPEED = 1500
MOVE_TIME = 40  # milliseconds
SERVO_COMMAND_BUFFER_TIME = 0.01 # seconds

XBOX_JOYSTICK_MIN = -32768
XBOX_JOYSTICK_MAX = 32767
XBOX_TRIGGER_MIN = 0
XBOX_TRIGGER_MAX = 1023

XBOX_SERVO_MAP = {
    ecodes.ABS_HAT0X: 0,
    ecodes.ABS_Y : 1,
    ecodes.ABS_RY : 2,
    ecodes.BTN_WEST : 3,
    ecodes.BTN_SOUTH : 3,
    ecodes.BTN_TL : 4,
    ecodes.BTN_TR : 4,
    ecodes.ABS_RZ : 5
}

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
SETUP_POSITIONS = [2070, 847, 3011, 655, 1838, 1806]
SETUP_MOVE_SPEED = 1200
SETUP_TIME = 2

current_positions = SETUP_POSITIONS[:]

SELECT_TIMEOUT = 0.01

def find_controller():
    devices = [evdev.InputDevice(path) for path in evdev.list_devices()]
    for device in devices:
        if "xbox" in device.name.lower() or "microsoft" in device.name.lower():
            print(f"Found controller: {device.name}")
            return device

    # If the controller is not found by name, list all devices for the user
    if not devices:
        print("No input devices found. Make sure your controller is connected.")
        return None

    print("Could not automatically find an Xbox controller. Please select a device:")
    for i, device in enumerate(devices):
        print(f"  {i}: {device.name} ({device.path})")

    try:
        choice = int(input("Enter the number of the device to use: "))
        if 0 <= choice < len(devices):
            return devices[choice]
        else:
            print("Invalid choice.")
            return None
    except (ValueError, IndexError):
        print("Invalid input.")
        return None

def calculate_checksum(data):
    return (~sum(data) & 0xFF)

def create_move_packet(servo_id, position, speed):
    time_ms = MOVE_TIME

    if position >= SERVO_LIMITS[servo_id - START_ID].max:
        position = SERVO_LIMITS[servo_id - START_ID].max
    
    elif position <= SERVO_LIMITS[servo_id - START_ID].min:
        position = SERVO_LIMITS[servo_id - START_ID].min

    packet = [
        0xFF, 0xFF, servo_id, 0x09, 0x03, 0x2A,
        position & 0xFF, (position >> 8) & 0xFF,
        time_ms & 0xFF, (time_ms >> 8) & 0xFF,
        speed & 0xFF, (speed >> 8) & 0xFF
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
    packet = [
        0xFF, 0xFF, servo_id, 0x04, 0x02, 0x38, 0x02
    ]
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
        print(f"│ {servo_id:2d} │  {position:7d}  │ {min_pos:6d} │ {max_pos:6d} │")
    print("└────┴───────────┴────────┴────────┘\n")

    print("  [Start] Quit")

def map_range(value, in_min, in_max, out_min, out_max):
    return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min

def main():
    controller = find_controller()
    if not controller:
        print("Controller not found. Exiting...")
        return

    print(f"\nReading events from: {controller.name}")

    port = input(f"Enter serial port [default: {UART_PORT}]: ").strip() or UART_PORT

    try:
        print(f"Attempting to connect to {port}...")
        serial_obj = serial.Serial(port, UART_BAUDRATE, timeout=0.1)
        print(f"Connected to {port} successfully")

        print("Enabling torque on all servos...")
        for i in range(NUMBER_OF_SERVOS):
            serial_obj.write(create_torque_packet(START_ID + i, True))
            time.sleep(SERVO_COMMAND_BUFFER_TIME)

        print("Read the servos angle...")
        for i in range(NUMBER_OF_SERVOS):
            servo_id = START_ID + i
            current_positions[i] = read_servo_position(serial_obj, servo_id)
            
        print("Moving all servos to initial position...")
        for i in range(NUMBER_OF_SERVOS):
            middle_position = (SERVO_LIMITS[i].min + SERVO_LIMITS[i].max) // 2
            current_positions[i] = middle_position
            serial_obj.write(create_move_packet(START_ID + i, middle_position, SETUP_MOVE_SPEED))
            time.sleep(SERVO_COMMAND_BUFFER_TIME)

        time.sleep(SETUP_TIME)

        controller_state = {}

        # Initialize the state for all axes the controller supports.
        for code, abs_info in controller.capabilities().get(ecodes.EV_ABS, []):
            controller_state[code] = abs_info.value
        
        while True:
            # Use select() for a non-blocking read of events.
            r, w, x = select.select([controller.fileno()], [], [], SELECT_TIMEOUT)
           
            # If there are events to read, process them.
            if r:
                for event in controller.read():
                    # Update our state dictionary with the new event value
                    if event.type == ecodes.EV_KEY or event.type == ecodes.EV_ABS:
                        controller_state[event.code] = event.value
            
            # Move servos here.
            for code, value in sorted(controller_state.items()):
                if code == ecodes.BTN_START and value == 1:
                    print("Shutting down...")
                    raise

                if code == ecodes.ABS_HAT0X: # arrow keys on the left bottom
                    servo_index = XBOX_SERVO_MAP[code]
                    step = int(POSITION_STEP/2) # slow down for servo 1
                    if value == 1:
                        current_positions[servo_index] += step
                        serial_obj.write(create_move_packet(START_ID + servo_index, current_positions[servo_index], MOVE_SPEED))

                    elif value == -1:
                        current_positions[servo_index] -= step
                        serial_obj.write(create_move_packet(START_ID + servo_index, current_positions[servo_index], MOVE_SPEED))          

                if code == ecodes.ABS_Y: # left joystick y axis
                    servo_index = XBOX_SERVO_MAP[code]
                    mapped_value = int(map_range(value, XBOX_JOYSTICK_MIN, XBOX_JOYSTICK_MAX, -POSITION_STEP, POSITION_STEP))

                    if mapped_value != 0:
                        current_positions[servo_index] += mapped_value
                        serial_obj.write(create_move_packet(START_ID + servo_index, current_positions[servo_index], MOVE_SPEED)) 

                if code == ecodes.ABS_RY: # right joystick y axis
                    servo_index = XBOX_SERVO_MAP[code]
                    mapped_value = int(map_range(value, XBOX_JOYSTICK_MIN, XBOX_JOYSTICK_MAX, -POSITION_STEP, POSITION_STEP))

                    if mapped_value != 0:
                        current_positions[servo_index] += mapped_value
                        serial_obj.write(create_move_packet(START_ID + servo_index, current_positions[servo_index], MOVE_SPEED)) 

                if code == ecodes.BTN_WEST and value == 1: # Y key
                    servo_index = XBOX_SERVO_MAP[code]
                    current_positions[servo_index] -= POSITION_STEP
                    serial_obj.write(create_move_packet(START_ID + servo_index, current_positions[servo_index], MOVE_SPEED))

                if code == ecodes.BTN_SOUTH and value == 1: # A key
                    servo_index = XBOX_SERVO_MAP[code]
                    current_positions[servo_index] += POSITION_STEP
                    serial_obj.write(create_move_packet(START_ID + servo_index, current_positions[servo_index], MOVE_SPEED))

                if code == ecodes.BTN_TL and value == 1: # LB
                    servo_index = XBOX_SERVO_MAP[code]
                    current_positions[servo_index] += POSITION_STEP
                    serial_obj.write(create_move_packet(START_ID + servo_index, current_positions[servo_index], MOVE_SPEED))

                if code == ecodes.BTN_TR and value == 1: # RB
                    servo_index = XBOX_SERVO_MAP[code]
                    current_positions[servo_index] -= POSITION_STEP
                    serial_obj.write(create_move_packet(START_ID + servo_index, current_positions[servo_index], MOVE_SPEED))

                if code == ecodes.ABS_RZ: # RT
                    servo_index = XBOX_SERVO_MAP[code]
                    current_positions[servo_index] = int(map_range(value, XBOX_TRIGGER_MIN, XBOX_TRIGGER_MAX, SERVO_LIMITS[servo_index].max, SERVO_LIMITS[servo_index].min))
                    serial_obj.write(create_move_packet(START_ID + servo_index, current_positions[servo_index], MOVE_SPEED*2)) 
            
            # Read the servo encoder value update to print and mitigate the offset.
            for servo_index in range(NUMBER_OF_SERVOS):
                # current_positions[i] = read_servo_position(serial_obj, START_ID + servo_id)
                if current_positions[servo_index] > SERVO_LIMITS[servo_index].max:
                    current_positions[servo_index] = SERVO_LIMITS[servo_index].max

                if current_positions[servo_index] < SERVO_LIMITS[servo_index].min:
                    current_positions[servo_index] = SERVO_LIMITS[servo_index].min

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
                    time.sleep(SERVO_COMMAND_BUFFER_TIME)
                serial_obj.close()
                print("Serial connection closed.")
            except Exception as e:
                print(f"Error while closing connection: {e}")

if __name__ == "__main__":
    main()
