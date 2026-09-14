# Joshua MHS — Model Hardware Interface

A local MCP interface for one Teensy 4.1 + TB6600 stepper. An LLM can discover
what the device does, submit a bounded position move, observe its result, and
choose its next action. This is an MHS-inspired prototype, pending the official
specification; it does not claim official MHS compatibility.

## Core interface

| Tool | Result |
| --- | --- |
| `list_devices()` | Configured devices, descriptions, tags and capabilities; no device I/O |
| `describe_device(device_id)` | Units, limits, resolution and feedback limitations; no device I/O |
| `read_state(device_id)` | Fresh emitted-step feedback, estimated degrees and command status |
| `write_position(device_id, position_degrees)` | Acceptance of a finite absolute target, rounded to whole steps |
| `stop_device(device_id)` | Driver-disable acknowledgment and invalidated session reference |

Call discovery and description first. After a write, poll `read_state` until
`status` leaves `moving`. `controller_target_reached` means the controller's
emitted step count reached the quantized target. `physical_position_verified`
is always false. The motor stays enabled after completion to hold position.
A new move can then be selected from the observations; overlapping moves are
rejected, with no queue or automatic retry.

Stop, timeout, or transport/feedback failure invalidates the reference. The LLM
cannot rearm or reset it. An operator must check the rig, re-establish the
reference, and restart the session. A failed disable reports
`stop_unconfirmed`, not successful physical stopping. Normal EOF, SIGINT and
SIGTERM request disable during executor shutdown.

## Architecture and configuration

```
MCP host → official Python MCP SDK → private subprocess pipe → C++ runtime
                                                            → TeensyBoard
                                                            → Teensy → TB6600
```

`server.py` only adapts tools and schemas. `runtime.cc` owns validation, degree
conversion, reference validity, and command state. `executor.cc` reads the
protobuf config once and independently monitors active moves every 20 ms.
Existing ROS actuator startup and teardown are bypassed: neither automatic
startup enable nor move-to-idle teardown is appropriate here. The ROS launcher
rejects API-enabled presets to avoid accidentally using that path.

The new `Config.hardware_api` section opts in and references an existing
actuator by name. Wiring, gear ratio and position limits remain in the existing
robot fields. MVP validation requires exactly one exposed Teensy STEP_DIR
channel 0. Additional devices, sensors, cameras, HTTP transport, application
experiments, and multi-user services are later work.

The example [preset](../config/config_preset/example/teensy_hardware_api.pbtxt)
uses the existing rig's wiring and operator-selected limits. Bench checks below
cover only the tested moves;
operators must review the limits for their mechanism. Existing presets expose
nothing by default. API-disabled or invalid configs fail startup before I/O.

## Build and verify without hardware

Use Joshua's Ubuntu 24 image (build with `docker compose build joshua-u24` if
needed). The standalone commands below do not mount host devices or request
privileged/GPU access. Run them from the repository root.

```bash
docker run --rm -v "$PWD:/workspace" -w /workspace \
  -v joshua_bazel-cache-u24:/root/.cache/bazel joshua:u24-jazzy \
  bash -lc 'source /opt/ros/jazzy/setup.bash &&
    bazel test --config=u24 --config=x86-base \
      --@rules_python//python/config_settings:python_version=3.12 \
      //mhs:runtime_test //mhs:executor_test &&
    mkdir -p dist/mhs && cp bazel-bin/mhs/executor dist/mhs/executor &&
    python3 -m venv .cache/mhs-venv &&
    .cache/mhs-venv/bin/pip install -r mhs/requirements.lock &&
    .cache/mhs-venv/bin/python mhs/mcp_test.py'
```

The C++ tests inject a test-only channel; there is no runtime fake-device mode.
The IPC and MCP integration tests launch the real executor **offline**, verify
schemas and discovery, and check that motion is rejected. The optional MCP SDK
is pinned separately from the ROS dependency environment; the lock file was
resolved for Python 3.12. It uses the maintained SDK v1 API.

Run the repository-wide CI task separately:

```bash
docker compose run --rm test-u24
```

## Test from the ChatGPT desktop app

Use the desktop app on the same Ubuntu computer as Joshua and the Teensy.
Complete the build/setup command above first. The local MCP configuration is
shared with Codex; see the [official MCP setup documentation](https://learn.chatgpt.com/docs/extend/mcp).

### Register Joshua and test discovery

Add the following table to `~/.codex/config.toml`, preserving other settings.
Replace `/ABS/PATH/Joshua` with the repository's absolute path (on the reference
bench, `/home/kjyoon/myWorkspace/Joshua`). If the Joshua table already exists,
edit it instead of adding a duplicate.

```toml
[mcp_servers.joshua]
command = "docker"
args = [
  "run", "--rm", "-i", "--init",
  "-v", "/ABS/PATH/Joshua:/workspace:ro",
  "-w", "/workspace",
  "joshua:u24-jazzy",
  "/workspace/.cache/mhs-venv/bin/python",
  "/workspace/mhs/server.py",
  "--executor", "/workspace/dist/mhs/executor",
  "--config", "config/config_preset/example/teensy_hardware_api.pbtxt"
]
```

Open **Settings → General → MCP servers** and find `joshua`. Enable its toggle.
If it is missing after editing the file, fully quit and reopen the desktop app.
Then ask:

> Use Joshua to list the configured devices and describe the stepper's
> capabilities and limits. Do not move anything.

This configuration starts offline discovery only. A device reported as
**disconnected** is expected here: the MCP connection works, but hardware
access is disabled. Joshua does not require an OpenAI API key; the host app
supplies the model. Other local stdio-capable MCP clients can use the same
Docker command and arguments. This server does not provide an HTTP endpoint.

### Enable hardware for a supervised session

Check wiring, the configured travel limits, the unloaded/non-gravity-loaded
mechanism, the coordinate reference, and an accessible independent stop.
Do not run another serial owner or the ROS launcher alongside the MHS executor.
Then add these two arguments to the same TOML array:

- Before `"joshua:u24-jazzy"`, insert `"--device=/dev/ttyACM0:/dev/ttyACM0",`.
- Add a comma after the config-path argument, then append
  `"--hardware-and-reference-confirmed"`.

Reload the connection as described below. Starting the hardware-enabled server
opens/configures the Teensy and requests driver disable; it does not submit a
position target. Keep hardware enablement limited to the supervised session,
and remove these two arguments afterward. Do not configure unattended hardware
restarts: the confirmation applies to the operator-checked session reference.

### Reload after changing the preset or MCP arguments

The executor reads its preset once at startup. Saving a `.pbtxt` file does not
update an already-running session.

1. Finish or stop any active move before reloading.
2. Open **Settings → General → MCP servers**.
3. Toggle `joshua` **off**, wait for it to disconnect, then toggle it **on**.
   This is the reload procedure when the app has no **Restart** button.
4. In a new chat, ask Joshua to describe the device again. Verify the reported
   limits before requesting motion. If they remain stale, fully quit and reopen
   the desktop app, then verify again.

The current free-rotation bench preset permits **0–1,890 degrees** and
**90 degrees per move**. Those bounds were explicitly expanded for a 90-degree
forward move from the retained five-turn position near 1,800 degrees. They are
experiment bounds for the operator-confirmed rig, not measured mechanical stops.
No rebuild is needed for a preset-only change.

### Request a move from the app

Start by asking for state only. A connected session must have fresh feedback and
an operator-confirmed reference before motion. For the requested relative move:

> Use Joshua to read the current stepper position and its limits. If the state
> is ready and a target 90 degrees forward is within both the position and
> per-move limits, move there. Wait for controller completion, then disable the
> driver. Do not reset zero or return to the starting position. Stop on any
> error; do not change limits or automatically retry a failed command.

`write_position` takes an **absolute** angle. The app implements this relative
request by reading the current estimated angle and adding 90 degrees. It must
not send an absolute target of 90 degrees when the current counter represents
approximately 1,800 degrees. Once the counter reaches the upper bound, another
forward move is outside this preset's range.

`stop_device` invalidates the session reference. Before further moves, the
operator checks that the reference is still trustworthy and reloads the session.
Reloading does not reset the Teensy's emitted-step counter.

### Establish zero, or retain a known reference

There is no homing sensor. To establish a new zero:

1. Disable the Joshua MCP connection and switch off TB6600 motor power.
2. Disconnect the Teensy's USB cable and any other power feeding the Teensy.
   Switching off only the TB6600 leaves a USB-powered Teensy's counter intact.
3. With the motor unpowered, gently place the shaft at the desired starting
   orientation; mark it if useful.
4. Reconnect the Teensy, enable the Joshua connection, and restore motor power.
5. Ask for state only: expect `emitted_steps: 0`, an estimated angle of zero,
   `status: ready_disarmed`, and `disable_acknowledged: true`.

Power cycling clears the counter; it does not physically home the mechanism.
Zeroing is unnecessary if the operator confirms an existing reference remains
valid and both the current position and requested target fit the reviewed
preset. Do not widen bounds just to suppress an unexplained startup error.

The adapter and executor are separate processes but share their container's
permissions in this MVP. Only the executor contains device code; this is not a
security boundary between an untrusted adapter and hardware. The private pipe
is not a network API.

## Physical limits of this MVP

The existing firmware has no encoder, homing, stall detection, acceleration
ramp, or communication-loss watchdog. Its velocity feedback does not indicate
whether a position move is complete; this runtime uses emitted step counts.
Counter discontinuities can reveal some resets, but a reset to the same count,
manual shaft motion, and missed steps cannot be detected reliably. The operator
must invalidate the session after such events.

The host enforces a configured move timeout while it is alive and communicating;
this is not a firmware watchdog or a hard real-time stopping deadline. A target
can continue after host failure or cable loss. Disable can release holding
torque and cannot prove physical stopping. No hardware execution or firmware
flashing is part of the software tests.

## Bench validation status

The supervised C++ executor test passed after the operator fully power-cycled
and re-referenced the Teensy:

- Startup read: 0 emitted steps, driver disabled.
- Requested 5 degrees: accepted target 44 steps; fresh feedback reached 44
  steps (approximately 4.95 degrees after quantization).
- Requested return to 0 degrees: fresh feedback reached 0 steps.
- Stop: disable acknowledged, reference invalidated; executor exited normally.

These readings confirm controller-level execution; the open-loop controller
cannot independently verify shaft position.
The subsequent MCP-to-hardware test also passed at controller level: all five
MCP tools were exercised, a 20-degree target reached 178 emitted steps
(approximately 20.025 degrees), the return reached 0, and stop acknowledged
disable. This used the official SDK client and stdio server against the real
executor and Teensy; it was not a ChatGPT application session. Each move finished
within about 50 ms of polling. The operator confirmed that the motor visibly
moved and returned during the 20-degree test. This confirms observed movement, not independently measured
angular accuracy.

The initial startup attempts safely rejected a retained counter of 29,940 steps
(3,368.25 degrees), outside the preset's 0–360 degree range, without sending a
position target. Startup errors now include the count and bounds, and a passing
regression test verifies that an out-of-range startup cannot authorize motion.

A later operator-authorized five-revolution MCP command used a temporary
0–1,800 degree range and reached 16,000 emitted steps before disable was
acknowledged. The checked-in preset was subsequently expanded to 0–1,890 degrees
with a 90-degree per-move limit for testing the next relative move from the app.
That preset change passed offline validation; it is not evidence that the app's
90-degree command has been executed.
