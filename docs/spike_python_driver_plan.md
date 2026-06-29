# Spike Python Driver Plan

Goal: Support Pybricks motors through the Python robot layer, replacing the separate
python_bridge process with Python actuator nodes.

## Phases (testable outcomes)

1) Phase 1 - Schema & config (completed)
2) Phase 2 - Python driver implementation (completed)
3) Phase 3 - Factory wiring (completed)
4) Phase 4 - ROS node integration (no hardware) (completed)
5) Phase 5 - Hardware smoke test (completed)

## Phase 1: Schema & config (focus)

Deliverables
- Add a Pybricks actuator type and config in `robot/action/proto/action.proto`.
- Optional: add explicit BLE/USB comm types in `robot/comm/proto/comm.proto`
  (or reuse `serial_config.port` with values like "A", "B", etc.).
- Add a small pbtxt example that uses the new actuator type.

Proposed edits
- `robot/action/proto/action.proto`
  - `ActuatorType` add `SPIKE_MOTOR = <new_id>`
  - `oneof action_config` add `SpikeMotorConfig spike_motor_config = <new_id>`
  - `message SpikeMotorConfig` fields (choose and confirm):
    - `string hub_id` (optional; for multi-hub BLE selection)
    - `string port` (e.g., "A", "B", "C", "D")
    - `bool use_ble` / `bool use_usb` (or use comm fields instead)
- `robot/comm/proto/comm.proto` (optional)
  - `CommType` add `BLE = <new_id>`, `USB = <new_id>`
  - Add `BleConfig` / `UsbConfig` messages if you want structured transport config.
- Add example pbtxt:
  - `config/config_preset/example/python_spike_actuator_example.pbtxt`

Test commands
1) Open the Docker shell:
   - `docker compose run --rm joshua-u22`
2) Build protos inside the shell:
   - `bazel build --config=u22 --config=x86-base //robot/action/proto:... //robot/comm/proto:... //config/proto:...`
3) Validate pbtxt parsing inside the shell:
   - `bazel run --config=u22 --config=x86-base //utils:config_loader -- config/config_preset/example/python_spike_actuator_example.pbtxt`
   - If `config_loader` is not available, use your existing Docker-backed config validation command.

Notes
- Keep transport config minimal for Phase 1; use whichever fields make implementation
  simpler in Phase 2 (BLE/USB selection can be refined later).

Completed work
- Added `SPIKE_MOTOR` actuator type and `SpikeMotorConfig` (`hub_id`, `port`).
- Added `BLE` to `CommType`.
- Added example config: `config/config_preset/example/python_spike_actuator_example.pbtxt`.

## Phase 2: Python driver implementation (decisions)

Decisions
- Control mode: absolute angle commands.
- Transport: direct Pybricks control (no helper script).
- Hub selection: `hub_id` only.
- Commanding: send immediately on each `set_action`.

Implementation notes
- Implemented a BLE transport that uploads a minimal Pybricks program and sends
  `SET <PORT> <ANGLE>` commands over stdin.

Completed work
- Added Pybricks driver: `robot/action/motors/drivers/pybricks_driver.py`.
- Added BLE transport: `robot/comm/pybricks_ble_transport.py`.
- Added unit tests: `robot/action/motors/drivers/pybricks_driver_test.py`.
- Added BLE smoke script: `tools/pybricks/pybricks_ble_smoke.py`.

## Phase 3: Factory wiring

Completed work
- Wired `SPIKE_MOTOR` into `robot/action/factory/action_factory.py`.
- Added factory test: `robot/action/factory/action_factory_test.py`.

## Phase 4: ROS node integration (no hardware)

Completed work
- Verified actuator subscriber logic with existing test flow (skips without rclpy).

## Phase 5: Hardware smoke test

Completed work
- BLE smoke test moved motor using `pybricks_ble_smoke`.
- ROS actuator subscriber drove motor A and B via topics.
