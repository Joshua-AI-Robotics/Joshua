# Joshua MHS — Model Hardware Interface

A local MCP interface for one configured Teensy 4.1 + TB6600 stepper. This is
an MHS-inspired prototype, pending the official specification; it does not
claim official MHS compatibility.

## Architecture

```text
MCP host → Python MCP SDK → stdio ROS bridge
         → configured ROS command topic → actuator_subscriber
         → bounded actuator session → ActionInterface → StepperDriver → BoardChannel
         ← configured ROS status topic ← driver acknowledgment / feedback
```

The MCP session manager launches the existing ROS actuator executable; it
does not implement motor I/O. `ros_bridge` replaces the former standalone
hardware executor. The ROS actuator node is the sole owner of the configured board,
uses the existing action factory and motor driver, and monitors active moves
every 20 ms even when the MCP client disconnects. Limits, wiring, conversion,
node assignment, topic names and QoS all come from the protobuf config.

`ros2/actuator_session.*` owns bounded-move policy; `StepperDriver` owns device
I/O and conversion. The exposed stepper requires `manual_lifecycle: true`:
startup disables the driver, enable follows a validated position target, and
shutdown disables without sending an idle-position target. Existing presets
retain their default lifecycle.

The current exposure remains limited to one Teensy STEP_DIR channel 0. The
[example preset](../config/config_preset/example/teensy_hardware_api.pbtxt)
configures an `ACTUATOR_SUBSCRIBER` node with reliable, volatile `STRING`
command and status topics. No raw Float32 subscription may bypass the exposed
actuator's bounded policy. Other, unexposed actuators retain the existing
Float32 command interface.

## Tools and outcomes

| Tool | Result |
| --- | --- |
| `list_devices()` | Configured descriptions, tags and capabilities |
| `describe_device(device_id)` | Units, limits, resolution and feedback limitations |
| `start_session(hardware_ready, reference_confirmed)` | Start the ROS actuator node, connect, and return disabled state |
| `end_session()` | Request disable, close the bridge, and stop the owned ROS node |
| `read_state(device_id)` | Fresh emitted-step feedback and command status |
| `write_position(device_id, position_degrees)` | Driver acceptance of a bounded absolute target |
| `stop_device(device_id)` | Disable acknowledgment and invalidated session reference |

Discover and describe first. After a write, poll `read_state` until `status`
leaves `moving`. `controller_target_reached` means the emitted-step counter
reached the quantized target; `physical_position_verified` is always false.
The motor stays enabled after completion. Overlapping moves are rejected.
Stop, timeout or feedback/transport failure invalidates the reference. An operator
must inspect the rig and provide fresh hardware/reference confirmation before
starting a new session. In managed mode, call `end_session`, then
`start_session` with that confirmation. Calling `start_session` on an active
session returns its state; it never silently restarts or rearms it.

A ROS publication is not an acknowledgment. Requests carry a unique ID, the
actuator session ID and a short expiration time. Replies correlate by request
ID. The node rejects expired requests and previous-session commands and caches
write results to prevent duplicate execution (4,096 write requests per session).
DDS command durability is volatile. A bridge acknowledgment timeout is an
uncertain outcome: it requests stop and refuses further writes. It never
replays motion automatically. Normal bridge EOF/SIGINT/SIGTERM requests stop;
the ROS node continues monitoring moves if the bridge is killed abruptly.

The command/status payload is JSON in `std_msgs/msg/String`, using the same
`operation`, `device_id`, and `position_degrees` fields as the MCP adapter, plus
`request_id`, `session_id`, and `expires_unix_ms`. Discovery returns the session
ID. A deadline must be in the future and at most five seconds away; the bridge
uses one second. This local prototype assumes synchronized host clocks. This
acknowledged envelope is needed because a scalar position message cannot
identify acceptance, rejection, or a replay. Both topic names come from the
exposed action's `node.subscriptions` and `node.publishers`.

## Build and test without hardware

From the repository root, using the Ubuntu 24 image:

```bash
docker run --rm -v "$PWD:/workspace" -w /workspace \
  -v joshua_bazel-cache-u24:/root/.cache/bazel joshua:u24-jazzy \
  bash -lc 'source /opt/ros/jazzy/setup.bash &&
    bazel test --config=u24 --config=x86-base \
      --@rules_python//python/config_settings:python_version=3.12 \
      //ros2:actuator_session_test //mhs:ros_bridge_test //mhs:session_test \
      //robot/action/motors/drivers:stepper_driver_test &&
    bazel build --config=u24 --config=x86-base \
      --@rules_python//python/config_settings:python_version=3.12 \
      //launcher:joshua_main //mhs:ros_bridge &&
    mkdir -p dist/mhs &&
    cp bazel-bin/mhs/ros_bridge dist/mhs/ros_bridge.new &&
    mv dist/mhs/ros_bridge.new dist/mhs/ros_bridge &&
    cp bazel-bin/ros2/actuator_subscriber dist/mhs/actuator_subscriber.new &&
    mv dist/mhs/actuator_subscriber.new dist/mhs/actuator_subscriber &&
    python3 -m venv .cache/mhs-venv &&
    .cache/mhs-venv/bin/pip install -r mhs/requirements.lock &&
    .cache/mhs-venv/bin/python mhs/mcp_test.py'
```

Tests use a real ROS command/status exchange and a real stepper driver with a
recording test channel; they never open hardware. Tests cover conversion,
completion, timeout after client loss, disable acknowledgment, invalid moves,
replays and stale requests. IPC and MCP tests check offline discovery and
motion rejection. Run repository CI separately with
`docker compose run --rm test-u24` (or `test-u22`).

## Connect the desktop MCP client

Replace paths below with your checkout. Rebuild the bridge before switching
from the old `--executor` configuration to `--bridge`.

```toml
[mcp_servers.joshua]
command = "docker"
args = [
  "run", "--rm", "-i", "--init",
  "--device=/dev/ttyACM0:/dev/ttyACM0",
  "-v", "joshua-mhs-locks:/run/joshua-mhs-locks",
  "-v", "/ABS/PATH/Joshua:/workspace:ro", "-w", "/workspace",
  "joshua:u24-jazzy", "bash", "-lc",
  "source /opt/ros/jazzy/setup.bash && exec /workspace/.cache/mhs-venv/bin/python /workspace/mhs/server.py --bridge /workspace/dist/mhs/ros_bridge --config config/config_preset/example/teensy_hardware_api.pbtxt --actuator-node /workspace/dist/mhs/actuator_subscriber --session-lock-dir /run/joshua-mhs-locks"
]
```

This managed configuration initially exposes discovery with `connected: false`.
No device is opened merely by connecting MCP. The Teensy must be plugged in for
Docker's device mapping to succeed. Toggle the MCP connection off/on after
updating these arguments or rebuilding the executables.

You can now do the whole workflow in the ChatGPT app:

1. Ask Joshua to list and describe devices.
2. Confirm that the rig is connected and clear, no other serial owner is
   running, and the position reference is valid. Ask it to call `start_session`
   and read state without moving. The tool requires both confirmation booleans.
3. Request a bounded move, then poll `read_state` for completion.
4. Ask it to `end_session`. Check `disable_acknowledged` and `errors`.

`start_session` launches only the configured actuator node, using the trusted
`--actuator-node` executable. Tool calls cannot choose a command, executable,
config path, topic or device path. Configuration is snapshotted when the MCP
server starts, so discovery and live control use the same preset. The bridge
switches to ROS internally; no app config edit or terminal command is needed.

The named lock volume prevents concurrent managed sessions from owning the
same serial path, including across MCP connections. Use the same lock volume
for every instance. This advisory lock does not cover independently started
launchers or other serial programs; do not run those concurrently. Node or
startup failure requires `end_session` and fresh operator confirmation before
another start. There are no automatic restarts, homing or firmware flashing.
Closing MCP requests disable and shuts down its owned node. Shutdown errors
remain visible and are not treated as proof that the motor stopped.

For offline-only discovery without managed tools, omit `--actuator-node`, the
device mapping and the lock volume. To connect to an independently launched
ROS graph, use `--connect-ros` instead of `--actuator-node`; these modes are
mutually exclusive.

## Supervised hardware session

Check wiring, reviewed travel limits, an accessible independent stop and the
coordinate reference before launching. Do not run the old standalone executor
or any other serial owner alongside the ROS node.

Managed MCP sessions use `start_session` as described above. Alternatively,
from a Joshua Docker development shell, an operator can launch the graph with:

```bash
bazel run --config=u24 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.12 \
  //launcher:joshua_main -- \
  --config=config/config_preset/example/teensy_hardware_api.pbtxt \
  --hardware_and_reference_confirmed
```

The launcher validates the exposure before starting nodes and passes this
session confirmation to its children. An API-enabled preset without this flag
is rejected. Starting the node disables the driver and reads the counter; it
does not submit a position target. The ROS node's container needs the hardware
mapping. The MCP bridge's container only needs access to the same ROS domain:
add `"--network=host"` before the image and append ` --connect-ros` to the end
of the shell command string in the MCP configuration. Both containers must use
the same `ROS_DOMAIN_ID` if customized. This is a trusted local ROS graph, not an access
control boundary.

For a relative move, read current state and add the desired displacement to
`estimated_position_degrees`; `write_position` always takes an absolute target.
The checked-in bench preset retains the operator-selected 0–1,890 degree range
and 90-degree per-move limit. These are experiment bounds, not measured stops.
Do not widen them to suppress unexplained feedback. After stop or a fault,
inspect the rig and obtain fresh reference confirmation. In managed mode,
end and start the session through MCP; for a standalone graph, restart the node
and reconnect the bridge. Preset edits require reloading the MCP server.

## Feedback and physical limits

There is no encoder, homing sensor, stall detection, acceleration ramp or
firmware communication-loss watchdog. Emitted steps estimate position; they
cannot detect missed steps or manual motion reliably. Disable releases holding
torque and cannot prove physical stopping. The ROS timeout is a host policy,
not a firmware watchdog: a target can continue after host failure or cable loss.

To establish a new zero, disconnect the session, remove motor power and all
Teensy power including USB, position the unpowered mechanism, then reconnect
and verify a zero counter before requesting motion. Power cycling resets the
counter; it does not physically home the mechanism. Keep a retained counter
only when the operator confirms that its reference is still trustworthy.

Earlier supervised bench checks validated the **former direct executor**
(5-degree, 20-degree and five-revolution moves). They are not hardware
validation of this ROS integration. Subsequent ROS bench results are recorded
below; no firmware was flashed during these tests.

### Bench conversion correction

The ROS bench test sent 800 additional pulses on each requested 90-degree
move. The operator observed 180 degrees on the repeated move and reported
SW1..SW6 as OFF/ON/OFF/ON/ON/OFF. This matches 1/8 microstepping in the
[DFRobot TB6600 guide, page 6](https://dfimg.dfrobot.com/nobody/wiki/0bcc0b661ce7750ff7d0134bfc3e88b3.pdf),
consistent with 1,600 pulses/revolution for the configured 1.8-degree motor.
The hardware API preset now uses `steps_per_degree: 4.4444447` instead of
`8.888889`; a 90-degree increment now requests 400 pulses. A subsequent
supervised MCP-to-ROS run advanced the counter from 1,600 to 2,000 steps,
reported controller completion, acknowledged disable, and shut down cleanly.
Physical confirmation of the corrected 90-degree shaft angle remains pending.

Controller acknowledgment in the earlier tests confirmed pulse counts, not
angular accuracy. The retained counter is not reset by this config change:
1,600 steps now represent approximately 360 degrees, rather than 180 degrees.
Recheck the physical reference before another supervised run. Other presets
have not been recalibrated, and the configured travel bounds are unchanged.


### Managed session verification

The managed MCP lifecycle was exercised on the connected Teensy without a
position command: discovery reported disconnected, `start_session` launched
the ROS actuator node and returned `ready_disarmed` with 2,000 emitted steps
(approximately 450 degrees), a repeated start reused the same session, and
`end_session` returned `disable_acknowledged: true` with no cleanup errors.
Discovery then reported disconnected again. The counter did not change.
