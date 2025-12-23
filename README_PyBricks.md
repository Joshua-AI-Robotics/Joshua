# PyBricks Integration Guide

This repo includes a Python bridge to control LEGO Spike Prime via Pybricks. Follow the steps below to set up firmware, dependencies, the hub-side helper script, and the ROS bridge for the first time.

## 1) Install Pybricks firmware on the hub
1. Go to https://code.pybricks.com/ and follow the instructions to flash Pybricks firmware onto your Spike Prime hub.
2. Power-cycle the hub after flashing. The stock LEGO firmware will not work with pybricksdev control.

## 2) Install host-side dependencies
Use the same Python that your ROS/bazel run will use (typically `/usr/bin/python3` from the ROS environment):
```bash
source /opt/ros/humble/setup.bash
python3 -m pip install "pybricksdev[usb,ble]"
```
If needed, grant BLE permissions to Python (one-time):
```bash
sudo setcap 'cap_net_raw,cap_net_admin+eip' /usr/bin/python3
```

## 3) Load the hub helper program
The helper script lives at `scripts/pybricks_spike_bridge.py`. It listens for simple text commands (`SET <PORT> <VALUE>`) and drives the motor; it also turns the hub LED green when running.

To upload and start it over BLE:
```bash
pybricksdev run ble scripts/pybricks_spike_bridge.py
```
Wait for the upload to finish, then hit Ctrl+C to free the BLE connection. The program keeps running on the hub.

USB is also supported by pybricksdev if the hub enumerates as `/dev/ttyACM*`:
```bash
pybricksdev run usb scripts/pybricks_spike_bridge.py
```

## 4) Set up environment for the bridge
In a fresh shell:
```bash
source /opt/ros/humble/setup.bash
export PYTHONPATH="/opt/ros/humble/lib/python3.10/site-packages:/opt/ros/humble/local/lib/python3.10/dist-packages:$(/usr/bin/python3 -c 'import site; print(site.getusersitepackages())')"
```

## 5) Run the Python bridge
Use the provided config preset for a single motor on port A:
```bash
PYTHON_BRIDGE_BACKEND=pybricks PYBRICKS_TRANSPORT=ble \
  bazel run ros2:python_bridge -- \
    python_bridge_node 99 config/config_preset/python_bridge_spike_usb.pbtxt \
    --ros-args --log-level python_bridge_node:=debug
```

## 6) Send a test command
In another ROS-sourced shell with the same PYTHONPATH:
```bash
ros2 topic pub spike/motor_A/command std_msgs/msg/Float32 "{data: 0.5}"
```
You should see the motor move (50% duty).

## 7) Drive from the RandomNoise model
The preset includes a RandomNoise model (node id 200) publishing to `spike/motor_A/command`. It expects a tick input:
- Start a tick publisher in one shell:  
  `bazel run ros2:noise_tick_publisher -- noise_tick_publisher 0`
- Start the inference node in another shell:  
  `bazel run ros2:inference -- noise_node 200 config/config_preset/python_bridge_spike_usb.pbtxt`
- Start the bridge as in step 5. The motor will receive random commands.

## Notes and tips
- The current config disables encoder polling; focus on outbound commands first. Encoders can be re-enabled later (they require a working stdin/stdout path).
- If BLE fails to accept writes (ATT errors), power-cycle the hub, re-run the helper upload, then restart the bridge.
- For visibility, keep the bridge in debug log level to see hub responses.
- If BLE is unreliable on your setup, switch `PYBRICKS_TRANSPORT=usb` and ensure the hub shows up as `/dev/ttyACM*` before running the bridge. USB has more reliable stdin/stdout.
