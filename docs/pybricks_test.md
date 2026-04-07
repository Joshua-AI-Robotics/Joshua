# Pybricks Test Guide (Beginner)

This guide walks you through verifying the Pybricks motor control end‑to‑end.
It assumes you are in the repo root.

## 1) Flash Pybricks firmware
1. Go to `https://code.pybricks.com/`.
2. Follow the instructions to flash Pybricks firmware to your Spike hub.
3. Power‑cycle the hub after flashing.

## 2) Install host dependencies
Use the system Python used by ROS:
```bash
source /opt/ros/humble/setup.bash
python3 -m pip install "pybricksdev[usb,ble]"
```

## 3) Find your hub name (BLE)
Turn on the hub so it advertises, then run:
```bash
python3 - <<'PY'
import asyncio
from bleak import BleakScanner

async def main():
    devices = await BleakScanner.discover(timeout=5)
    for d in devices:
        if d.name:
            print(d.address, d.name)
asyncio.run(main())
PY
```
Use the printed name as your `hub_id` (example: `Pybricks Hub`).

## 4) Quick BLE smoke test (no ROS)
This uploads a tiny Pybricks program to the hub and sends one angle command.
```bash
bazel run //tools/pybricks:pybricks_ble_smoke -- "Pybricks Hub" A 90
```
Note: the smoke test tool lives under `tools/pybricks`.
Expected output includes:
```
READY
OK A 90.0
```
Your motor should move to 90 degrees.

## 4.5) Upload the hub helper program (required for Python bridge)
Run this once to load the helper on the hub:
```bash
pybricksdev run ble scripts/pybricks_spike_bridge.py
```
Wait for upload to finish, then press Ctrl+C to free the BLE connection.

## 5) Run the ROS actuator subscriber
In one terminal:
```bash
conda deactivate
source /opt/ros/humble/setup.bash
bazel run //ros2:actuator_subscriber_py -- actuator_subscriber 901 config/config_preset/example/python_spike_actuator_example.pbtxt
```
You should see:
```
READY
Actuator subscriber node started with 1 actuators for node_id 901!
```

## 6) Send a single ROS command
In a second terminal:
```bash
conda deactivate
source /opt/ros/humble/setup.bash
ros2 topic pub spike/motor_A/command std_msgs/msg/Float32 "{data: 90.0}"
```
The motor should move to 90 degrees.

## 7) Continuous motion (sine wave)
Use the included script for motor A and motor B (run in two terminals):

Motor A:
```bash
conda deactivate
source /opt/ros/humble/setup.bash
/usr/bin/python3 scripts/spike_wave_publisher.py --topic /spike/motor_A/command
```

Motor B:
```bash
conda deactivate
source /opt/ros/humble/setup.bash
/usr/bin/python3 scripts/spike_wave_publisher.py --topic /spike/motor_B/command --offset 60 --amplitude 30
```

## Troubleshooting
- If BLE connect fails: power‑cycle the hub and retry.
- If you see `ModuleNotFoundError: rclpy._rclpy_pybind11`, you’re using conda Python.
  Use `/usr/bin/python3` after `conda deactivate`.
- If the motor doesn’t move, double‑check the port (`A/B/C/D`) and hub name.

### BLE connection fail
Intel Bluetooth adapters (specifically the Intel AX210, AC 9260, and similar chips) are notorious for two specific issues on Linux that would cause your TimeoutError:

Stale GATT Caches: The Intel driver often caches the "services" of a device the first time it sees it. If you saw the Spike Hub before you installed the Pybricks firmware, the Intel driver "remembers" it as a standard LEGO hub. When pybricksdev scans for a "Pybricks" service, the Intel driver says "I know that device; it doesn't have that service," and ignores the hub entirely.

Firmware Lockups: Under heavy scanning (like when 3 Samsung TVs are nearby), the Intel Bluetooth firmware can occasionally enter a state where it reports device addresses but fails to resolve "Advertisement Data" (the metadata like names and UUIDs).

To work around these issues, you can use an external Bluetooth dongle instead of the built-in adapter:

1. Connect an external Bluetooth dongle to your system
2. Check available Bluetooth interfaces:
   ```bash
   hciconfig -a
   # You will see two interface (e.g. hci0 and hic1)
   ```
3. Disable the built-in Bluetooth adapter (typically `hci0`) to force the system to use the dongle:
   ```bash
   sudo hciconfig hci0 down
   ```

This will ensure the system uses only the external dongle for Bluetooth connections.
