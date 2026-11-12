# Config

The protobuf config is the **single source of truth** for a robot setup. One
`.pbtxt` file describes the whole system — actuators, boards, sensors, AI
policies, simulation backend — and the launcher instantiates ROS 2 nodes from
it. If a value describes a particular robot, it belongs here, not in code.

```bash
bazel run //launcher:joshua_main -- --config config/config_preset/so100/sim_passive.pbtxt
```

> A preset may drive **real hardware** whatever it is named and whatever
> `operation_mode` it declares. Read the preset before running it — see the
> hardware-safety section of [AGENTS.md](../AGENTS.md).

## Validate config integrity without launching Joshua

Build `//config:joshua_validate_config` in the development container, then
run the binary with exactly one `--config PATH` argument (or
`--config=PATH`). For example:

```sh
docker compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm -T joshua-u22 bazel run --config=u22 --config=x86-base --@rules_python//python/config_settings:python_version=3.10 //config:joshua_validate_config -- --config /workspace/config/config_preset/so100/teleoperate.pbtxt
```

The command emits one JSON report on stdout with `scope=config_integrity`,
`stage`, `result`, canonical `status_code`, and a limitation that implementation
support and hardware readiness were not checked. Exit 0 means parse and
`ValidateConfig` passed; exit 1 means invalid content or ordinary input
rejection; exit 2 means usage error; exit 3 means internal or output failure.
The input must resolve to a regular file, and reading is bounded to 10 MiB.
Symlinks to regular files work in standalone path mode. Parser failures include
bounded, one-based line and column details in the JSON `message`. The MCP adapter
uses the same executable with its opt-in `--config-root-fd FD` mode, which accepts
only a relative reference below an inherited directory descriptor and rejects
symlinks in every component. This command does not initialize nodes or
drivers and does not open any device named by the config. It retains the current
validator behavior, including acceptance of an empty config. For MCP use, see
[the read-only stdio adapter](../tools/mcp/README.md).

`//config:joshua_inspect_config` supplies config-free `list_supported_components`
and confined `list_devices`/`describe_device` operations through the MCP adapter.
Its metadata shares implementing factory tables and never invokes their
creators. Device reports show declared configuration with compatibility and
hardware readiness unverified. The inventory and report are bounded; see the
[adapter guide](../tools/mcp/README.md) for identities and limits.
The MCP adapter separately offers `list_supported_hardware` for
[curated models](../docs/SUPPORTED_HARDWARE.md), including aliases, drivers,
example presets, and intended support scope. `list_hardware_test_records`
retrieves [historical runs](../docs/TESTED_HARDWARE.md) by stable hardware ID.
Tests retain environments, measurements, and limitations separately from model
metadata. Neither query establishes readiness or changes config validation.

## Layout

The mixed Teensy [scalar preset](config_preset/example/teensy_mixed_scalar.pbtxt)
uses `FRAME_STREAM` and one `MCU_BUS` process for actuator and scalar consumers.
`joshua_wire_scalar_config` selects `board_name`, intrinsic `function_id` and
`period_us`; it does not declare an actuator channel. The firmware catalog must
advertise that exact sensor/schema and period support. Subscriptions require an
explicit aggregate `telemetry_bytes_per_second` reservation. The candidate uses
100 Hz acquisition and 50 Hz latest-value publication; coalescing is observable.

Images advertising configurable watchdogs require explicit
`firmware.communication_watchdog_us` and `target_watchdog_us` (10000..10000000).
`joshua_wire_host_policy.heartbeat_period_ms` must be positive and its period plus
the ordinary-operation budget, polling budget and scheduling guard must be below
the communication watchdog. A reserved internal GET_STATUS maintains communication
and detects faults; it never refreshes targets. The mixed stepper configuration
requires `require_explicit_enable: true`. Startup validation checks one process
per serial bus before opening any device. These configured budgets remain subject
to physical qualification.

- `proto/` — the schema.
  - `robot.proto` — top-level `Robot`; composes the others.
  - `config.proto` — `General`, including `operation_mode`, which the launcher
    branches on.
  - `ai.proto` — inference and policy configuration.
  - `physical_layout.proto` — poses, axes, and frames.
- `config_preset/<robot>/` — checked-in presets, one per robot and task
  (`so100/`, `example/`).
- `config_utils.h` — `LoadConfig`, which opens a `.pbtxt` and parses it into a
  `config::Config`. Parsing only; semantic checks happen later, in the
  factories and `launcher/node_manager/`.

## Responsibilities

- Describe *what* a robot is made of and how it is wired.
- Stay the only place a robot-specific value appears: ports, joint limits, gear
  ratios, servo IDs, topic names, model paths.
- Carry enough structure that an inconsistent setup can be rejected during
  startup — in the factories and `launcher/node_manager/` — rather than at first
  motion.

## Non-Goals

- Behavior. How a motor moves is driver logic in [robot/](../robot/README.md).
- Secrets or machine-local paths. Presets are committed and shared.
- Generated artifacts. Do not hand-edit anything produced by codegen.

## Adding or changing a preset

1. Start from the nearest existing preset in the same robot directory.
2. Change the schema in `proto/` only if the field genuinely does not exist —
   a new preset should rarely need one.
3. For anything you intend to run repeatedly during development, pick a preset
   you have read and confirmed declares no real devices. Neither the filename
   nor `operation_mode: MODE_SIMULATION` is that confirmation — both cross the
   safe/unsafe boundary in this repo. What counts as safe is defined once, in
   the hardware-safety section of [AGENTS.md](../AGENTS.md); check the preset
   against that list rather than against a copy of it here.

## JoshuaWire serial protocol selection

JoshuaWire (JW) `0.2.0` is the sole Joshua protocol. Omitted `protocol`
selects JW for AM243, Teensy 4.1 and ESP32 serial boards. Prefer explicit fields
inside the `Board` entry:

```text
protocol: JOSHUA_WIRE
firmware { min_proto_version: 4 }
```

The semantic release version is `0.2.0`; `min_proto_version` checks the on-wire
revision, which is `4`. JW1 support is removed. Rename explicit
`JOSHUA_WIRE_V2` configs to `JOSHUA_WIRE` and update old minimums to `4`.
JW initializes with a fresh session reset, then
IDENTIFY, paged discovery/dependencies, profile metadata and status reconciliation,
then ACTUATOR.CONFIGURE; initialization leaves channels disabled.
Feetech does not accept this selection. The AM243 TI-demo host path is retired.
See [firmware build instructions](../firmware/README.md#joshuawire-serial).

### Serial timing

Serial timing belongs in `comm.serial_config`, independently of board identity:

```text
serial_config {
  port: "/dev/ttyUSB0"
  baudrate: 115200
  message_adapter: JOSHUA_WIRE
  exchange_timeout_ms: 100
  post_open_settle_ms: 2000
}
```

The framed JoshuaWire exchange timeout includes queue wait, writing, and receiving
a complete frame. Omitted means 100 ms; explicit values must be 1..INT_MAX
milliseconds. `post_open_settle_ms` is 0..INT_MAX milliseconds, defaults to zero,
and runs once after physical open, before exposing the connection.

All raw, vendor, and framed operations share one nonblocking serial owner.
Optional `serial_config.io_policy` controls its software budgets and admission:

| Field | Default | Meaning |
| --- | --- | --- |
| `read_timeout_ms` | 500 | Total raw read budget, including queue wait |
| `read_idle_timeout_ms` | 100 | Maximum gap while an active raw/vendor read awaits bytes |
| `write_timeout_ms` | 100 | Total send-only or flush budget |
| `transaction_timeout_ms` | 100 | Total CALLER_SIZED write/read budget |
| `max_pending_operations` | 16 | Active plus queued operations, allowed range 1..1024 |
| `max_pending_bytes` | 1048576 | Reserved request plus response bytes, range 1..16777216 |
| `max_transfer_bytes` | 65536 | Request plus response limit per operation, range 1..1048576 and no larger than pending bytes |

All four timeouts must be 1..INT_MAX milliseconds. Progress renews only the idle
budget, never the total deadline. JoshuaWire uses `exchange_timeout_ms` for its
total deadline and no separate idle deadline. Request-only framed requests reserve their
maximum response budget at admission. Oversized/full-queue requests fail before
publication. Raw/vendor partial responses return an error through the existing
interfaces; internal operation results retain actual read/write progress.

Consumers of a physical port must agree on baudrate, settle delay, and effective
`io_policy`, including defaults. Only JoshuaWire users must also agree on
`exchange_timeout_ms`; omitted timeout and explicit 100 ms are equivalent.
CALLER_SIZED and BYTE_STREAM consumers do not select a JoshuaWire timeout.
Preflight checks identical configured paths without opening hardware. At runtime,
resolved device aliases share one owner; conflicts fail before another open.
The last consumer releases the owner. A retained faulted connection is not
silently reopened; release all consumers before creating a fresh generation.

These defaults replace the former fixed raw/vendor timeouts. Configure them for
the device cadence, USB/UART behavior, maximum response size, and scheduling
margin. They are software limits, not measured hardware latency guarantees.
For example, 2520 bytes require about 109 ms of 8N1 wire time at 230400 baud;
raw reads must allow the full scan and gaps between its fragments.

**ESP32 config migration:** the board-specific 2-second sleep has been removed.
For a USB bridge that resets the MCU on open, set `post_open_settle_ms: 2000`
as in the checked-in ESP32 example. Native USB or other links may need a
different value; board type no longer guesses it. The manual JW probe's
`--settle_ms` remains an additional diagnostic wait, not a runtime setting.

### Continuous frame capability (P3a)

`transport_type: FRAME_STREAM` with `message_adapter: JOSHUA_WIRE` selects the
host integration capability for a future shared MCU coordinator. It exposes
`FrameStreamTransport`, rather than `MessageTransport` or `ByteStream`. Existing
board/session consumers and demo presets remain request-only; this capability
alone does not enable sensor streaming or implement the coordinator.

```text
comm_type: SERIAL
transport_type: FRAME_STREAM
serial_config {
  port: "/dev/serial/by-id/<configured-controller>"
  baudrate: 115200
  message_adapter: JOSHUA_WIRE
  frame_policy {
    frame_idle_timeout_ms: 100
    max_reply_frames: 4
    max_notification_frames: 32
  }
}
```

`frame_policy` is accepted only in this mode. Its omitted fields use the shown
software defaults. Partial-frame idle timeout must be 1..INT_MAX milliseconds;
reply and notification capacities must each be 1..256 frames. `io_policy` still
controls physical write time and admission limits; `max_transfer_bytes` must be
at least 96. `exchange_timeout_ms` is rejected: the forthcoming coordinator owns
the complete operation/retry deadline above this duplex link.

All consumers of a physical port must select the same receive mode and effective
policies. A continuous receiver cannot share that port with raw/vendor or
request-only JoshuaWire users, including through path aliases. Frame-mode users
share one lease; exactly one coordinator must consume it. The last lease closes
the receiver. There is no automatic reopen or protocol reset.

Replies have reserved capacity and are returned before notifications. Pending
notifications with the same session/token/function are replaced by the most
recent arrival; when notification capacity is full, the oldest whole notification
is dropped. Counters report both forms of loss. Reply overflow faults the link
and wakes receivers instead of silently losing reply ownership. Profile metadata,
subscription identity, sequence ordering and freshness still require coordinator
validation; a received frame alone is not a usable sensor observation.

## JoshuaWire over EtherCAT

Select `protocol: JOSHUA_WIRE` and `transport_type: MESSAGE_AND_CYCLIC`.
The paired endpoint supplies CoE management plus correlated PDO target/feedback;
`CYCLIC` alone is rejected; the legacy TI-demo host path is retired. Endpoint facts belong in
`comm.ethercat_config`; do not add `am243_config` to a JW board.

The complete [AM243 JW EtherCAT example](config_preset/example/am243_jw_ethercat_demo.pbtxt)
configures one software-only STEP_DIR channel and a Float32 position-command
subscriber on `am243_jw_joint_1/position`. Replace `ethercat0` with the intended
NIC and confirm `slave_index` before running: this preset opens a real bus.
It requires the separate `jw` or `jw-soes` EtherCAT firmware; the default
UART/TI echo image is incompatible. The current AM243 channel does not drive
STEP/DIR GPIOs. Its timing values are illustrative and require qualification
on the intended hardware.

All seven timing fields are required, positive and at most `INT_MAX` microseconds.
The cycle must exceed `process timeout + max(state timeout, mailbox-step budget)
+ scheduling guard`. Operation/response timeouts must exceed one cycle. Runtime
mailbox transfers span multiple cycles; these checks establish budget consistency,
not hard-real-time feasibility. `response_timeout_us` bounds each adapter exchange,
including its queue wait. It is not a firmware watchdog setting.

For the software-only bring-up example, build the separate
[AM243 EtherCAT artifact](../firmware/am243/joshua_dual_transport/README.md#opt-in-jw-ethercat-profile)
with deliberately explicit watchdog intervals, e.g. command progress 2000000 µs
and target freshness 1000000 µs as used by the native integration test. These are
not motor-safety recommendations. Watchdog intervals are not advertised in the
current descriptor; confirm the flashed artifact's settings and account for
management latency before enabling. The host does not replay old targets to
feed watchdogs: fresh SET_TARGET calls are required while enabled.

An optional `pdo_region` contains all four `output_offset_bytes`,
`input_offset_bytes`, `output_size_bytes`, `input_size_bytes` values. It is an
exact assertion against discovery, not permission to reinterpret another slave's
bytes. JW requires 112-byte input/output regions. Without it, discovery supplies
offsets. Legacy `am243_config`, `am243_ethercat_config` and `MOTOR_TI_DEMO`
are rejected with migration errors. Their protobuf names/numbers remain allocated
for diagnostics; they are not executable compatibility paths. The old TI-demo
preset was removed, not silently converted to a different firmware protocol.

Every discovered slave must pass the JW descriptor/mapping check before the bus
enters OP, including unused slaves (which remain on stop images). Duplicate board
claims on a slave and mixed timing/protocol policies on a NIC are rejected.
Consumers of one NIC must run in one node process. The factory caches one master;
board teardown releases only its endpoint, and the last lease closes the bus.

The example timing budgets are illustrative, not a qualified production policy.
Account for the [master-side NIC timing limitation](../robot/comm/ethercat/README.md#known-master-side-nic-timing-issue)
and qualify deadlines on the intended host and slave hardware.
Initialization does not flash firmware. The preset is covered by parsing and
semantic validation tests, which do not open hardware.

## Sensor configuration

Each `single_perceptions` entry declares `sensor_name`, `sensor_type`, and one
concrete config: `opencv_config` for `IMAGE`, `lds01_config` for `RANGE_SCAN`, or
`sts3215_encoder_config` for `POSITION`. Sensor names also identify output
perception packets. Legacy `perception_type` and `camera`/`encoder`/`lidar`
wrappers are no longer accepted.

For example, position feedback references a configured board channel:

```text
sensor_name: "joint_1"
sensor_type: POSITION
sts3215_encoder_config { board_name: "arm_bus" channel: 1 }
```

The board owns serial settings and servo IDs. Position readings retain the
channel's native units; the old encoder operational limits were unused and are
not part of the new sensor config. Actuator limits remain configured separately.
OpenCV keeps its camera index and image settings in `opencv_config`; LDS01 keeps
its transport settings in `lds01_config.comm`.
Camera pixel format (`opencv_config.fourcc`) must contain exactly four characters,
such as `MJPG` or `YUYV`. Config validation checks this before any nodes launch;
the camera must support the selected format.

One serial bus must belong to one node process. Position sensors use
`POSITION_PUBLISHER`; `ACTUATOR_SUBSCRIBER` only executes action commands and
does not read or publish sensors. Reading sensors from an actuator's bus in a
separate process is not supported by the current bus ownership model.
The `teleoperate` preset reads a separate `leader_bus` on `/dev/ttyACM1`, while
the follower actuators use `/dev/ttyACM0`. Its leader publishers and follower
subscribers share `sts3215_servo_<joint>/position` topics, carrying native
position values as `Float32` commands directly between the two nodes.
Preset tests validate declared dependencies and serial-port ownership without
opening hardware.
For locally present serial devices, ownership and timing checks resolve path
aliases using filesystem metadata, consistently with the runtime factories.
Absent devices use normalized paths so offline validation remains available;
aliases with missing targets cannot establish that two paths name one device.

Serial `MESSAGE` communication requires an explicit
`serial_config { ... message_adapter: CALLER_SIZED }` for vendor drivers,
or `message_adapter: JOSHUA_WIRE` for Joshua-owned MCU firmware. ESP32, Teensy
and AM243 serial presets select the current revision-4 engine and firmware. Existing external serial MESSAGE configs must add this field;
there is no implicit framing choice. `BYTE_STREAM` leaves it unspecified.
`exchange_timeout_ms` applies only to `JOSHUA_WIRE`; fixed-length transactions
use `io_policy.transaction_timeout_ms` and `read_idle_timeout_ms`. See [communication](../robot/comm/README.md) for
transaction and receive-ownership contracts.

`config::ValidateConfig` in [validation.h](validation.h)
accepts the full `config::Config` and orchestrates separate checks for node assignments,
board/channel references, serial settings, and bus ownership. These checks use
resource dependencies rather than sensor measurement types. A sensor can
require board channels, direct communication, both, or neither. Sensor config
checks and dependency extraction are separate private helpers in that module.
Adding a driver updates those helpers; the shared resource checks
remain independent of sensor types. Factories keep defensive construction checks
for direct callers, without depending on the validation module.
There is no central sensor-to-publisher allowlist; node validation checks that
node types are specified and each node ID has one consistent type.


Position publishers and actuator subscribers select compiled ROS message types
using the existing `ros2_data_type` field. No field mappings are required.
See [typed ROS messages](../ros2/README.md#typed-position-and-actuator-messages)
for supported types, fixed conversion rules, JointState units and names, and
hardware/model constraints. Unsupported message/driver combinations are rejected
before hardware initialization; existing Float32 presets remain compatible.

## Actuator motion packets

Trajectory actions use `action { joint { joint_name: "sts_motor_1" position: 2004 } }`.
`JointCommand` replaces scalar and complex action packets. Native units remain
the default, preserving existing config limits and numeric topic values. See
[packet contracts](../ros2/utils/packet_parser.md) for SI units, driver capability
limits, torque-enable presets, and migration details.

ROS endpoints contain only their topic string and `ros2_data_type`. The selected
node defines the interpretation: actuator scalar position topics use native units,
while JointState uses SI and selects the configured actuator by message name.
Position encoding remains inside JointCommand for internal consumers. Scalar
producers must convert normalized outputs before publishing.

### JoshuaWire host coordinator policy

For a Joshua `FRAME_STREAM` board, `joshua_wire_host_policy` configures the shared
protocol queue. It is rejected on request-only/vendor boards. Omitted optional
fields resolve to these software defaults:

| Field | Default | Bound / meaning |
| --- | --- | --- |
| `operation_timeout_ms` | 200 | Queue plus all exact-byte attempts, 1..INT_MAX |
| `attempt_timeout_ms` | 50 | One write/reply attempt, no larger than operation budget |
| `polling_timeout_ms` | 40 | Total polling budget, no larger than operation budget |
| `max_attempts` | 2 | 1..4, identical IDs/bytes only |
| `max_pending_operations` | 16 | 1..1024 ordinary operations, including active |
| `max_pending_bytes` | 4096 | 192..1048576 reserved request plus maximum reply bytes |
| `max_priority_burst` | 8 | 1..1024 before considering lower priority with sufficient slack |
| `scheduling_guard_ms` | 1 | Positive scheduling margin, at most INT_MAX |
| `max_notifications` | 32 | 1..256 diagnostic latest raw envelopes |
| `max_streams` | 16 | 1..255 shared subscriptions (P3c) |
| `telemetry_bytes_per_second` | 0 | Explicit aggregate complete-frame reservation; zero prevents subscribing |

ESTOP has an independent single admission slot. All MCU handles must agree on
effective policy, including defaults. Polling fairness requires enough deadline
slack; admission and deadlines report overload instead of promising an infeasible
workload. Physical control/stop latency and telemetry capacity still require P6
qualification. Runtime board/perception integration follows in P5.

## C++ ROS logging

`general.ros2_log_mode` selects `ROS2_LOG_BOTH` (default), `ROS2_LOG_ROS`, or
`ROS2_LOG_GLOG`. It controls C++ node `JOSHUA_LOG` calls; Python and direct driver
logging are unchanged. See [logging behavior](../ros2/README.md#c-node-logging).

## Host digital GPIO

`HOST_GPIO` uses `host_gpio_config` with an exact `profile_id`/`profile_version`
and named `pins`, each with a purpose, physical header position or qualified
signal, direction, polarity and optional bias. Outputs require explicit logical
`initial_value` and `shutdown_value`. The board API exposes digital pins separately
from motor channels. Offline validation rejects pin conflicts (including aliases
and different board names), power/ground/reserved pins and unsupported policies.

See the [GPIO API and profile guide](../robot/board/host_gpio/README.md),
[Pi 5 board example](config_preset/example/pi5_gpio.pbtxt) and
[Orin Nano P3768 example](config_preset/example/orin_nano_gpio.pbtxt).
These board-only examples do not launch a digital ROS node; device drivers use
`BoardFactory::GetOrCreate` and `OpenDigitalPin`. Creating the board acquires real
GPIO lines and applies initial output values. Host boards omit `comm`, firmware,
protocol and motor channels. Existing MCU numeric pin configs remain unchanged.

## MCU pin-source wiring

Teensy 4.1 and ESP32-DevKitC V4/WROOM-32 accept an optional board-level
`pin_profile { id: "pjrc/teensy41" version: 1 }` or
`espressif/esp32-devkitc-v4-wroom32`. Each STEP/DIR channel can use:

```text
step_dir { max_pulse_rate_hz: 1000 }
step_dir_wiring {
  step { signal: "LEFT.GPIO2" }
  direction { firmware_pin: 3 }
  enable { connector: "LEFT" header_pin: 6 }
}
```

Teensy `LEFT`/`RIGHT` are component-side outer headers with USB at the top;
positions 1..24 count from the USB end. ESP32 uses the documented J2/J3
positions 1..19. Explicit native zero is valid. Resolution validates output
capability, reserved pins, and ownership, then sends the existing numeric
JoshuaWire configuration; firmware framing is unchanged. Mixing named wiring
and legacy numeric pin fields is rejected. Existing numeric-only presets remain
valid without a profile. These profiles claim only the listed connectors, not
underside, USB-host, Ethernet or internal pads.
