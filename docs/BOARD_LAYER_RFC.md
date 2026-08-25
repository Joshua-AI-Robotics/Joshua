# Board Layer RFC: Motor × Board × Transport Decomposition

Status: Draft (design only — no implementation in this document)
Companion to: [ARCHITECTURE.md](ARCHITECTURE.md), [am243_ethercat.md](am243_ethercat.md)

## 1. Purpose

Make the actuator stack fully configurable across three independent axes:

- **Motor** — what physically moves (STS3215 servo, NEMA17 stepper, Spike
  motor, future BLDC).
- **Board** — the controller that runs the low-level motor loop (AM243,
  Teensy, Arduino, or the MCU inside a smart servo).
- **Transport** — how the host reaches that controller (serial, EtherCAT,
  Ethernet/UDP, SPI, BLE).

Today these axes are conflated: `ActuatorType::AM243_ETHERCAT_ACTUATOR` bakes
a board and a transport into one enum value, and each new combination needs a
new driver class plus a new `ActionFactory` switch arm. This RFC introduces a
**board layer** between the motor drivers and the comm layer so any valid
(motor, board, transport) combination is a config choice, not a code change.

It also defines the firmware side of the contract: what MCU binaries look
like, when they are flashed, and how the runtime verifies that the flashed
firmware and the host config agree before any motor moves.

## 2. Scope

In scope:

- The `BoardChannel` seam and `robot/board/` layer (interfaces, factory,
  instance caching).
- Proto schema: top-level `boards`, motor-centric `Actuator`, the comm and
  drive legs, `FirmwareSpec`.
- The canonical host↔firmware channel protocol (`joshua_wire_v1`) and the
  cyclic (EtherCAT/PDO) variant.
- Firmware architecture, build variants, flashing tooling, and the IDENTIFY
  handshake.
- Migration of the existing STS3215 and AM243 paths.

Out of scope (future work):

- Real AM243 actuator firmware and its final PDO layout (tracked separately;
  see `am243_ethercat.md` — only the TI demo mapping is validated today).
- **Full** perception-layer migration — designed in §6.6–§6.8 with **optional**
  board binding (§6.6.1): driver-direct for standalone cameras/lidars;
  `board_name` + `channel` only when the sensor shares a `Board` with
  actuators. Scheduled in §10 Phase 6. **Partial** migration is in scope
  earlier: `Sts3215Encoder` shares the Feetech servo bus with actuators, so
  it is co-migrated in Phase 4 (§5.6) — bus safety cannot be deferred.
- Motion profiles, coordinated multi-axis trajectories, and real-time
  guarantees of the host cyclic loop.
- Auto-flashing at runtime (explicitly rejected; see §9).

## 3. Where Joshua is today

```
config .pbtxt
   └─ Actuator { actuator_type, comm{...}, am243_ethercat_config{...} }
        │
   ActionFactory  (robot/action/factory)
        │  switch on ActuatorType → picks driver, calls CommFactory
        ├─ Sts3215Driver        ←─ Serial            (CommFactory::CreateSerial)
        └─ Am243EthercatDriver  ←─ EthercatTransport (CommFactory::CreateEthercatTransport)
                │
        Am243PdoCodec (robot/action/motors/drivers/am243_pdo_codec)
```

| Capability | Status | Location |
| --- | --- | --- |
| Generic serial transport | ✅ working | `robot/comm/serial/` |
| Generic EtherCAT transport (SOEM, split LRD/LWR) | ✅ working | `robot/comm/ethercat/` |
| STS3215 driver (protocol + motor logic fused) | ✅ working | `robot/action/motors/drivers/sts3215_driver.*` |
| AM243 EtherCAT driver (TI demo PDO only) | ⚠️ smoke binaries only | `robot/action/motors/drivers/am243_ethercat_driver.*` |
| **Board abstraction (shared controller instances)** | ❌ missing | — |
| **Motor drivers decoupled from transports** | ❌ missing | drivers hold `Serial`/`EthercatTransport` directly |
| **Canonical host↔MCU channel protocol** | ❌ missing | — |
| **Firmware artifacts + flash tooling** | ❌ missing | — |
| **Firmware/config handshake at init** | ❌ missing | — |

Structural problems this RFC fixes:

1. **Combinatorial explosion.** A stepper (driving a TB6600) on Arduino over
   serial, the same stepper on AM243 over EtherCAT, and a servo on Teensy
   over EtherCAT would each need a new `ActuatorType`, driver class, and
   factory arm —
   N_motors × N_boards × N_transports enum values. The layered design is
   additive: N_motors + N_boards + N_transports files.
2. **No shared board instances.** `ActionFactory` creates one
   `EthercatTransport` per actuator, but a real AM243 controls several joints
   through *one* PDO image on *one* master socket. Only serial has sharing,
   via a TU-local `PortResources` singleton hidden inside `comm_factory.cc`.
3. **Codec in the wrong layer.** `am243_pdo_codec` encodes the *board
   firmware's* wire contract, not motor behavior, yet lives under
   `robot/action/motors/drivers/`.
4. **No firmware/config agreement check.** Nothing stops the host from
   driving a board flashed with the wrong firmware or an old protocol.
5. **Orphaned bus lifecycle.** `ConfigureSlaves()` / `StartCyclic()` are
   called only from the smoke binaries' `main()`s. The config-driven path
   (`.pbtxt → ActionFactory → CommFactory`) stops at `Init()`, so the first
   `SetPosition` through `actuator_subscriber` fails with "not configured" —
   the AM243 demo has only ever worked via smoke binaries. Bring-up
   sequencing has no owner today; the board layer gives it one (§10 Phase 3).

## 4. Design goals

- One file per motor type, one per board, one per transport — never one per
  combination.
- Boards are first-class, **shared** config objects. **Actuators** always bind
  to `(board_name, channel)` — board binding is mandatory (§6.3). **Board-backed
  perceptions** use the same binding when they share a `Board` resource (§6.6);
  standalone sensors (cameras, most lidars) stay **driver-direct** with no
  `board_name` (§6.6.1).
- All robot-specific meaning (joint mapping, limits, motor params) lives in
  host `.pbtxt` config — never in firmware. Flash once per wiring change;
  reconfigure freely without reflashing.
- Fail fast: every mismatch (wrong firmware, wrong transport, incompatible
  motor/channel pairing, missing servo on a bus) is caught at `Init()`, with
  an actionable error, before any motor moves.
- No plugin/registration framework. With Bazel + protobuf `oneof`s, explicit
  factory switches per layer are cheap, greppable, and type-checked. The win
  is orthogonal axes, not dynamism.

## 5. Target architecture

### 5.1 The four layers

```
                        ┌──────────────────────────────┐
                        │   robot config (.pbtxt)      │
                        │  boards: [...]  actuators:[…]│
                        └──────┬────────────────┬──────┘
                               │                │
                RUNTIME PATH   │                │   FLASH-TIME PATH (occasional)
                               ▼                ▼
                      ┌────────────────┐   ┌─────────────────┐
                      │ ActionFactory  │   │  tools/flash    │
                      └───────┬────────┘   └────────┬────────┘
                              │                     │ picks artifact by
                              ▼                     │ (board_type, transport)
      ═══════════ HOST ═══════════                  ▼
      ┌──────────────────────────┐          arduino-eth-v1.hex
      │  Motor driver            │          am243-ethercat-joshua-v1.bin
      │  "what moves"            │          teensy-ethercat-joshua-v1.hex
      │  StepperDriver,          │                  │
      │  Sts3215Driver           │                  │ avrdude / uniflash /
      └────────────┬─────────────┘                  │ teensy_loader_cli
                   │ BoardChannel                   │
                   │ SetTarget(pos/vel/torque)      │
                   ▼                                │
      ┌──────────────────────────┐                  │
      │  Board controller        │                  │
      │  "what runs the loop"    │◄─── shared ───┐  │
      │  Am243Board, TeensyBoard,│     codec     │  │
      │  ArduinoBoard,           │  joshua_wire  │  │
      │  FeetechBusBoard         │     _v1       │  │
      └────────────┬─────────────┘               │  │
                   │ Comm handle                 │  │
                   ▼                             │  │
      ┌──────────────────────────┐               │  │
      │  Comm transport          │               │  │
      │  "how bytes move"        │               │  │
      │  Serial, EthercatT.,     │               │  │
      │  UdpTransport, Spi…      │               │  │
      └────────────┬─────────────┘               │  │
      ═════════════│═ wire (USB/eth/SPI) ════════│══│═════
                   ▼                             │  ▼
      ┌──────────────────────────┐               │ (flashed once)
      │  transport module        │  ═ FIRMWARE ═ │
      │  (compile-time variant)  │               │
      ├──────────────────────────┤               │
      │  protocol codec          │◄──────────────┘
      │  (same joshua_wire_v1)  │   compiled into BOTH sides
      ├──────────────────────────┤
      │  channel dispatcher      │
      ├──────────────────────────┤
      │  motor backend           │
      │  step/dir gen, PWM,      │
      │  servo bus master        │
      └────────────┬─────────────┘
                   ▼
              TB6600 ─► stepper    (or PDO joint, servo horn, …)
```

| Layer | Axis it owns | One file per… |
| --- | --- | --- |
| Motor driver | motor physics, units, limits, calibration | motor type |
| Board controller | firmware bring-up, codec choice, channel mux | board |
| Protocol codec | wire message schema | protocol version (mostly shared) |
| Comm transport | raw byte/frame movement | transport type |

Composition, not multiplication:

```
                 StepperDriver            ◄── written ONCE
                /      |       \
               ▼       ▼        ▼
   ArduinoBoard     Am243Board   TeensyBoard          ◄── one per board
      /      \          |             |
     ▼        ▼         ▼             ▼
  Serial  UdpTransport  EthercatTransport            ◄── one per transport
```

Adding a fourth board adds one board file and zero motor files; adding a new
motor adds one motor file and zero board files.

### 5.2 Directory layout (target)

```
robot/
  action/
    factory/            ActionFactory (resolves board_name → channel → driver)
    interfaces/         ActionInterface, ActuatorInterface (unchanged)
    motors/drivers/     stepper_driver.*  sts3215_driver.*  (motor semantics only)
  board/                                   ── NEW LAYER ──
    interfaces/         board_interface.h, board_channel.h
    factory/            board_factory.*  (instance cache keyed by board name)
    proto/              board.proto
    am243/              am243_board.*  am243_pdo_codec.*  (moved from motors/drivers)
    arduino/            arduino_board.h/.cc  (JoshuaWireBoard subclass:
                        identity constructor plus a DTR-reset settle in
                        CreateTransport — the one Uno-specific bring-up
                        fact; MCU only, drive peripherals like a TB6600
                        are a Channel.drive fact, never in this name)
    teensy/             teensy_board.h  (header-only JoshuaWireBoard
                        subclass — a one-line constructor passing identity,
                        nothing else; MCU only, a comm peripheral like an
                        EasyCAT shield is a Board.comm fact, never in this
                        name) ── NEW, §10 Phase 5 ──
    joshua_wire/        joshua_wire_board.*  (shared IDENTIFY handshake +
                        CONFIGURE_CHANNEL + channel dispatch for every
                        joshua_wire_v1 MCU board — teensy/ and arduino/
                        both subclass this rather than reimplementing it;
                        §7.3, revised) ── NEW, §10 Phase 5 ──
    feetech_bus/        feetech_bus_board.*  (Feetech register protocol)
    host_gpio/          host_gpio_board.*  (Jetson/Pi pins; no comm, no firmware)
    frame/              frame_transport.h, serial_frame_transport.*  (the
                        FrameTransport seam JoshuaWireBoard depends on, not
                        a concrete transport — §7.3) ── NEW, §10 Phase 5 ──
  comm/                 serial/  ethercat/  udp/  spi/  factory/  (unchanged role)
firmware/
  common/               joshua_wire_v1.h/.c (wire codec: shared with host,
                        same repo, Bazel cc_library + PlatformIO lib) +
                        backend_stepdir.h/.cpp (drive backend: shared
                        across every firmware image with a STEP_DIR
                        channel — MCU-vendor-agnostic since it's plain
                        digitalWrite/pinMode, §7.3, revised) ── NEW ──
  am243/                ti_ethercat_simple_demo_v1/  (existing TI vendor demo;
                        stays as-is — vendor code never migrates to the
                        Joshua firmware pattern, §7.3)
  teensy/41/            main.cpp, channel_table.c, transport_serial.*,
                        platformio.ini (-I src, so firmware/common/'s
                        backend_stepdir.cpp can see this project's own
                        channel_table.h) ── NEW, §10 Phase 5 ──
  arduino/uno/          main.cpp, channel_table.c, transport_serial.*,
                        platformio.ini (backend_stepdir pulled from
                        firmware/common/, same as Teensy — Uno R3 serial
                        variant; hardware verification still open)
tools/
  flash/                config-driven flasher + firmware manifest
```

Directory name convention: the host-side directory matches the board type
alone (`teensy/`, not `teensy41/`) since `TeensyBoard`'s C++ is
model-agnostic — it only speaks `joshua_wire_v1` over a `FrameTransport`,
and as of the `JoshuaWireBoard` extraction (§7.3) that's now literally true
of the code, not just true in spirit: `TeensyBoard` itself is two methods.
The firmware-side directory is per exact chip (`teensy/41/`) since that's
where a model actually matters (pin count, peripherals, toolchain target);
a future Teensy model would add `firmware/teensy/40/` reusing the same
host class.

### 5.3 Contracts (interfaces)

The seam between motor drivers and boards:

```cpp
// robot/board/interfaces/board_channel.h
enum class TargetMode { kPosition, kVelocity, kTorque };

struct ChannelFeedback {
  float position = 0.0f;   // channel's native unit (steps, ticks)
  float velocity = 0.0f;
  uint32_t fault_flags = 0;
};

// One motor slot on one board. How the command reaches the controller
// (serial frame, UDP frame, EtherCAT PDO field, servo bus register write)
// is the board's problem.
class BoardChannel {
 public:
  virtual ~BoardChannel() = default;
  virtual absl::Status Enable() = 0;
  virtual absl::Status Disable() = 0;
  virtual absl::Status SetTarget(TargetMode mode, float value) = 0;
  virtual absl::StatusOr<ChannelFeedback> ReadFeedback() = 0;
};
```

```cpp
// robot/board/interfaces/board_interface.h
class BoardInterface {
 public:
  virtual ~BoardInterface() = default;
  // Opens comm, runs IDENTIFY handshake, pushes CONFIGURE_CHANNEL setup.
  virtual absl::Status Init(const robot::board::Board& config) = 0;
  virtual absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) = 0;
  virtual absl::Status Teardown() = 0;
};
```

`BoardFactory::GetOrCreate(board_config)` returns a **cached instance per
board name** — two actuators on the same board share one instance and one
comm handle. This replaces (and generalizes) the TU-local serial
`PortResources` cache in `comm_factory.cc`.

Caching by board name is necessary but not sufficient. Some transports are
**bus-exclusive**: an EtherCAT NIC has exactly one master, and
`ExchangeProcessData()` moves the whole bus image, not one slave's slice —
two boards daisy-chained on one NIC are two slaves under one master. So the
comm layer needs its own cache one level down: `CommFactory` returns the
same `EthercatTransport` for the same `interface_name` (today it constructs
a new SOEM master per call, and two `ecx_init()`s on one NIC fight over the
raw socket). The resource hierarchy:

```
NIC ─► one master transport ─► one cyclic loop ─► N boards (slaves) ─► M channels each
```

Serial already follows this pattern (`PortResources` keyed by port);
EtherCAT gets the equivalent keyed by interface name, and the cyclic loop
lives with the shared master transport, not the board (§5.8).

Unit convention: the channel speaks the board's native unit (steps, ticks);
the motor driver owns the degrees↔native conversion because steps/rev,
microstepping, and gear ratio are motor+mechanics facts from the actuator
config. The conversion lives on exactly one side of the seam.

Capabilities, not subclasses: a board that cannot do torque control returns
`UnimplementedError` from that mode (and reports it in the IDENTIFY
capability bits so `Init` can catch it early). Same motor driver either way.

### 5.4 Motor driver example (board- and transport-agnostic)

```cpp
// robot/action/motors/drivers/stepper_driver.h — sketch
class StepperDriver : public ActuatorInterface {
 public:
  StepperDriver(std::shared_ptr<robot::board::BoardChannel> channel,
                const robot::action::Actuator& config);

  absl::Status Init() override { return channel_->Enable(); }

  absl::Status SetPosition(float angle_deg) override {
    if (angle_deg < operational_lower_limit_ || angle_deg > operational_upper_limit_)
      return absl::OutOfRangeError(...);
    return channel_->SetTarget(TargetMode::kPosition, DegreesToSteps(angle_deg));
  }
  // SetSpeed → kVelocity; SetTorque → UnimplementedError for open-loop steppers;
  // SetIdlePosition → SetPosition(idle); Teardown → channel_->Disable().

 private:
  std::shared_ptr<robot::board::BoardChannel> channel_;  // the ONLY way out
  float steps_per_degree_, gear_ratio_, idle_position_;
  float operational_lower_limit_, operational_upper_limit_;
};
```

No comm headers, no board headers, no `#ifdef`s. The factory decides which
channel implementation gets injected.

### 5.5 Comm and drive: the two legs of every command

Every command crosses two very different legs, named by role:

- **Comm** — host↔board communication: how the PC reaches the controller.
- **Drive** — board→motor actuation: how the controller moves the hardware.
  ("Drive" as the electronics world uses it — a TB6600 is sold as a stepper
  *drive*.)

```
        COMM  (host ↔ board)                   DRIVE  (board → motor)
  PC ────────────────────────► MCU ─────────────────────────────► motor hardware
     serial / UDP / EtherCAT        STEP/DIR pulses / PWM / UART
                                    servo bus / CAN / GPIO
     carried by: Comm transport     executed by: firmware motor backend
     specified in: Board.comm       specified in: Board.channels[i].drive
```

`joshua_wire_v1` lives entirely on the comm leg: comm is how the bytes
travel, the wire codec is what the bytes mean, and the drive is what the
board does with them once decoded. The drive leg never carries frames.

The drive leg splits between flash time and runtime:

- **Wiring facts** (which MCU pins drive which TB6600, that a servo bus is on
  UART1) are baked into the firmware variant — they cannot change without a
  soldering iron.
- **Tunables** (max pulse rate, direction inversion, bus baud, servo ID per
  channel) live in `Channel.drive_config` and are pushed at `Init()` via
  `CONFIGURE_CHANNEL` protocol commands.
- The IDENTIFY handshake reports per-channel drives ("ch0: STEP_DIR,
  ch2: SERVO_BUS_UART"); `Init` fails fast if config and firmware disagree.

`DriveInterface` also anchors motor↔channel validation in one place:

```cpp
// ActionFactory, before constructing the driver:
//   STEPPER_NEMA17 requires STEP_DIR;  STS3215 requires SERVO_BUS_UART;
//   DC_MOTOR requires PWM_DC.
ABSL_RETURN_IF_ERROR(ValidateMotorChannel(actuator.motor_type(), channel_cfg.drive()));
```

### 5.6 Boards without a separate MCU (STS3215 and friends)

"No board" never means bypass the layer — it means **the board is whatever
controller the comm leg terminates in.** Each STS3215 contains its own MCU
speaking the Feetech register protocol; the servo bus is the degenerate case
where comm and drive are the same physical link:

| Concept | General board | `FEETECH_BUS` board |
| --- | --- | --- |
| `Board.comm` (comm leg) | serial/UDP/EtherCAT to MCU | the serial bus itself |
| Firmware protocol codec | `joshua_wire_v1` / PDO layout | Feetech register protocol |
| `Channel` (drive leg) | motor slot on the MCU | one servo ID on the bus |
| IDENTIFY handshake | firmware name + proto version | ping servo ID + read model-number register |
| `FirmwareSpec` / flashing | `tools/flash` artifact | omitted — vendor firmware |

One `FeetechBusBoard` instance per serial port; each daisy-chained servo is a
channel keyed by `servo_id`. The register-protocol encoding moves out of
`Sts3215Driver` into this board; the driver shrinks to motor semantics
(tick↔degree, limits, calibration, idle pose). Bus serialization (one
request/response in flight on the half-duplex UART) gets an owner: the
board's internal bus mutex.

The mutex only works if *all* bus traffic goes through the board.
`Sts3215Encoder` (perception layer) currently writes raw Feetech frames to
the same shared `Serial` from encoder-publisher timers; after the port it
reads through `FeetechBusBoard` (channels already expose `ReadFeedback()`)
or, at minimum, takes the board's bus lock — otherwise the half-duplex race
the mutex exists to prevent returns through the perception side door
(§10 Phase 4). Today's serial cache is also keyed by port *and* baudrate,
so two baudrates on one port would open the device twice; one board
instance per port closes that hole too.

**Host-direct GPIO (Jetson Orin, Pi 5) — the second degenerate case.** Here
the controller is the host computer itself: a motor wired straight to the
host's header (STEP/DIR into a TB6600, or PWM, off Jetson Orin / Pi 5 pins)
has nothing to communicate with. The comm leg vanishes and the drive
executes in-process:

| Concept | General board | `HOST_GPIO` board |
| --- | --- | --- |
| `Board.comm` (comm leg) | serial/UDP/EtherCAT to MCU | omitted — in-process calls |
| Firmware protocol codec | `joshua_wire_v1` / PDO layout | none — no comm leg, no frames |
| `Channel` (drive leg) | motor slot on the MCU | pin group on the host header |
| IDENTIFY handshake | firmware name + proto version | probe GPIO chip / claim pins at `Init()` |
| `FirmwareSpec` / flashing | `tools/flash` artifact | omitted — the "firmware" is this process |

`HostGpioBoard` channels implement `BoardChannel` by driving pins directly
(libgpiod, hardware PWM peripherals). The motor driver cannot tell the
difference: the same `StepperDriver` runs over Arduino (driving a TB6600),
over AM243+EtherCAT, or over bare host pins — that is the whole point of the
seam. The honest caveat: userspace Linux is a poor step-pulse generator.
Bit-banging STEP at tens of kHz from a non-RT process jitters badly, so
realistic backends are hardware PWM channels, the Pi 5's RP1, or a
kernel/RT helper, with pulse rates capped until one exists (§12.8).

Keeping `board_name` mandatory (rather than optional with an embedded
`comm{}` fallback) keeps `ActionFactory`, validation, and caching on a single
code path. The rule generalizes: every motor names a board, and the board
type says **where the control loop runs** — an external MCU (`AM243`,
`TEENSY41`, `ESP32`, `ARDUINO_UNO`), the motor's own MCU (`FEETECH_BUS`),
the host itself (`HOST_GPIO`), or nowhere (`MOCK`, for tests). "Doesn't need a board" always resolves to
one of these degenerate cases, never to bypassing the layer. Future smart
CAN motors follow the same pattern as a `CAN_BUS` board.

### 5.7 ROS 2 topic-backed boards

Some "boards" are already complete robot subsystems with their own ROS 2 API:
TurtleBot 3/OpenCR, TurtleBot 4/iRobot Create 3, Clearpath Jackal/Husky,
Stretch, and similar vendor-integrated bases. Joshua should still route them
through the board layer. The board is the subsystem that owns the low-level
control loop; the transport is ROS 2 topics/services/actions rather than a
serial frame, EtherCAT PDO image, or GPIO call.

This keeps the same rule as the MCU cases: application code depends on a
capability (`MobileBase`, `JointGroup`, `ReadableOdometry`), while the board
adapter hides the vendor API.

| Concept | Joshua-firmware board | ROS 2 topic-backed board |
| --- | --- | --- |
| `Board.comm` (comm leg) | serial/UDP/EtherCAT/SPI to firmware | ROS 2 namespace + topic/service/action names |
| Firmware protocol codec | `joshua_wire_v1` / PDO layout | vendor ROS 2 message contract |
| `Channel` (drive leg) | motor slot on MCU | subsystem endpoint: base velocity, joint group, gripper |
| IDENTIFY handshake | firmware name + proto version | discover required ROS 2 graph endpoints and message types |
| `FirmwareSpec` / flashing | `tools/flash` artifact | omitted — vendor software stack owns deployment |

Example: TurtleBot 4 should not be modeled as two raw wheel motors first.
The iRobot Create 3 base already owns wheel control, safety, odometry, dock,
bumper, cliff, and battery behavior. Joshua should model it as a board with
one mobile-base channel:

```proto
boards {
  name: "create3_base"
  board_type: ROS2_VENDOR_ROBOT
  comm {
    comm_type: ROS2
    ros2_config {
      namespace: "/"
      domain_id: 0
    }
  }
  channels {
    index: 0
    drive: MOBILE_BASE_VELOCITY
    ros2_endpoint {
      command_topic: "/cmd_vel"
      odometry_topic: "/odom"
      battery_topic: "/battery_state"
    }
  }
  ros2_vendor_robot_config {
    vendor: "irobot_create3"
  }
}
actions {
  single_actions {
    mobile_base {
      base_name: "turtlebot4_base"
      board_name: "create3_base"
      channel: 0
      max_linear_velocity_mps: 0.3
      max_angular_velocity_radps: 1.5
    }
  }
}
```

For a ROS 2 board, `Board::Init()` does graph-level validation instead of a
firmware handshake:

1. Resolve the configured namespace and topic/service/action names.
2. Verify required publishers/subscribers/services/actions appear before a
   timeout.
3. Verify message types match the adapter (`geometry_msgs/Twist`,
   `nav_msgs/Odometry`, `sensor_msgs/BatteryState`, `control_msgs` actions,
   etc.).
4. Start subscriptions and keep a latest-feedback cache for
   `ReadFeedback()`.

`SetTarget()` publishes or calls the vendor API:

```
 MobileBaseDriver.SetVelocity(vx, wz)
   └► channel_->SetTarget(kPlanarVelocity, {vx, wz})
        └► Ros2VendorChannel publishes geometry_msgs/Twist to /cmd_vel
             └► vendor base controller owns wheel control and safety
```

This is not a bypass around the board layer. It is the same abstraction with a
different transport and a vendor-owned control loop. Raw wheel-motor support
can still be added later for platforms where Joshua owns the motor controller,
but vendor robot bases should begin at the subsystem boundary the vendor
already exposes.

### 5.8 Runtime behavior: frame-based vs cyclic boards

Two transport families, hidden below `SetTarget`:

**Frame-based (serial, UDP, SPI, BLE)** — `SetTarget` encodes a frame via the
shared codec, sends it through the comm handle, and waits for the firmware's
ACK (or times out into a `Status` error):

```
 StepperDriver.SetPosition(90°)
   └► channel_->SetTarget(kPosition, 4500)
        └► encode: [A5][len][v1][SET_TARGET][ch][mode][4500f][crc]   ── same bytes on any wire
             └► comm_->Send(frame)
                  ├─ Serial: write() /dev/ttyUSB0     ├─ UDP: sendto() 192.168.1.50:5555
                  └─ SPI: ioctl(SPI_IOC_MESSAGE)
                       └► MCU decodes (same codec) → dispatcher → step gen pulses TB6600
```

The comm type is chosen **once, in `Board::Init`** (a small switch over
`cfg.comm().comm_type()`), never per command.

**Cyclic (EtherCAT)** — the master exchanges a fixed process-data image with
all slaves every cycle regardless of new commands. `SetTarget` performs no
I/O; it stages the value in the board's slice of the bus image, and the bus
master's single cyclic loop ships it:

```
 SetTarget(ch0, 4500)      SetTarget(ch1, -200)          (any time, any thread)
        ▼                        ▼
   ┌─────────────── staged output image ───────────────┐
   │ ch0: pos 4500 │ ch1: pos -200 │ ch2: … │ ch3: …   │   one image, whole board
   └──────────────────────┬────────────────────────────┘
                          │  every cycle (e.g. 1 kHz)
                          ▼
        transport_->ExchangeProcessData()   ── SOEM split LRD/LWR frames ──► AM243
                          ▲
   ┌────────── latest input image ─────────┐ ◄── feedback decoded per channel
   └───────────────────────────────────────┘
```

This is why boards must be shared instances — and why the loop belongs to
the shared master transport, not the board (§5.3): N actuators on one AM243
are N channels writing into one image, and two AM243s daisy-chained on one
NIC are two `PdoRegion` slices of the *same* image shipped by the same loop
on the same master socket. A per-board cyclic loop is impossible on a
shared bus.

## 6. Config schema

### 6.1 `robot/board/proto/board.proto` (new)

```proto
// Values name the native controller only — comm peripherals (an EasyCAT
// shield) belong to Board.comm and drive peripherals (a TB6600 stepper
// drive) to Channel.drive. Baking a peripheral into the board type would
// re-conflate the axes this RFC separates.
enum BoardType {
  BOARD_INVALID = 0;
  AM243 = 1;
  TEENSY41 = 2;           // comm peripheral (EasyCAT) lives on Board.comm
  ARDUINO_UNO = 3;        // drive peripheral (TB6600) lives on Channel.drive
  FEETECH_BUS = 4;        // STS/SCS smart-servo bus; the "board" is the servo's own MCU
  SPIKE_HUB_BLE = 5;      // Pybricks hub; vendor firmware over BLE
                          // (REMOVED in §10 Phase 9 — never implemented; the
                          //  host-side driver lives in tools/pybricks)
  HOST_GPIO = 6;          // no external controller; host pins drive the motor (§5.6)
  MOCK = 7;               // tests; channels are in-memory fakes
  ROS2_VENDOR_ROBOT = 8;  // vendor subsystem controlled through ROS 2 topics (§5.7)
}

enum DriveInterface {     // the drive leg: how the board moves the motor (§5.5)
  DRIVE_INVALID = 0;
  STEP_DIR = 1;           // pulse/direction stepper drive (TB6600 …)
  PWM_DC = 2;
  SERVO_BUS_UART = 3;     // Feetech/Dynamixel-style register bus
  CAN = 4;
  PDO_JOINT = 5;          // slot in a cyclic EtherCAT process-data image
  MOBILE_BASE_VELOCITY = 6;  // planar base velocity endpoint (cmd_vel/odom)
  JOINT_GROUP = 7;           // vendor joint trajectory/action endpoint
}

message Channel {
  uint32 index = 1;
  DriveInterface drive = 2;
  oneof drive_config {
    StepDirConfig step_dir = 10;    // max_pulse_rate_hz, invert_dir, enable_active_low
    ServoBusConfig servo_bus = 11;  // servo_id
    PwmConfig pwm = 12;             // frequency_hz, deadband
    HostGpioConfig host_gpio = 13;  // gpio chip + pin numbers (HOST_GPIO boards)
    Ros2EndpointConfig ros2_endpoint = 14;  // topic/service/action names (§5.7)
  }
}

message FirmwareSpec {
  string name = 1;                  // e.g. "arduino-eth"
  uint32 min_proto_version = 2;
  // Omitted entirely for vendor-firmware boards (FEETECH_BUS).
}

message Board {
  string name = 1;                  // referenced by actuators; cache key
  BoardType board_type = 2;
  robot.comm.Comm comm = 3;         // comm leg: how the PC reaches the controller
                                    // (omitted for HOST_GPIO / MOCK)
  repeated Channel channels = 4;    // drive leg: what each slot drives
  FirmwareSpec firmware = 5;
  oneof board_config {
    Am243Config am243_config = 10;  // slave_index, pdo_mapping, plus the PDO region
                                    // override (offsets/sizes) that today's
                                    // Am243EthercatConfig carries — kept so presets
                                    // can pin regions instead of trusting
                                    // GetPdoRegion auto-discovery
    Ros2VendorRobotConfig ros2_vendor_robot_config = 11;
  }
}
```

### 6.2 `config/proto/robot.proto`

```proto
message Robot {
  robot.action.Action actions = 1;
  robot.perception.Perception perceptions = 2;
  ros2.trajectory.Trajectories trajectories = 3;
  repeated robot.board.Board boards = 4;    // NEW
}
```

### 6.3 `robot/action/proto/action.proto` — Actuator becomes motor-centric

```proto
enum MotorType {                    // replaces board/transport-flavored ActuatorType
  MOTOR_INVALID = 0;
  STS3215 = 1;
  STEPPER_NEMA17 = 2;
  SPIKE_MOTOR = 3;        // (REMOVED in §10 Phase 9 along with Spike support)
  MOCK_MOTOR = 4;
}

message Actuator {
  string actuator_name = 1;
  uint64 id = 2;
  MotorType motor_type = 3;
  string board_name = 4;            // replaces embedded comm{}
  uint32 channel = 5;
  // limits, idle position, motor-specific config (steps_per_rev, gear_ratio, …)
}
```

`ActuatorType::AM243_ETHERCAT_ACTUATOR` and the embedded `Comm` field are
kept working but deprecated during migration (§10, Phase 1), then removed.

> **TODO(hmoon):** During migration the runtime carries **two config shapes**
> in parallel. **Legacy:** `actuator_type` + embedded `comm{}` (e.g.
> `STS3215_SERVO` — still live until Phase 4). **New:** leave `actuator_type`
> unset (proto default `ACTUATOR_INVALID` = 0 — *not* "broken"; it means "use
> the board layer") and set `motor_type` + `board_name` + `channel` with
> transport on `boards{}` (e.g. AM243 → `MOTOR_TI_DEMO` → `TiDemoDriver`,
> Phase 3 done). `ActionFactory` and `node_generator` both branch on this;
> see `robot/action/factory/action_factory.h` and
> `node_generator/node_generator.cc`.

### 6.4 Example preset

```proto
robot {
  boards {
    name: "bridge_1"
    board_type: ARDUINO_UNO
    comm { comm_type: ETHERNET_UDP udp_config { host: "192.168.1.50" port: 5555 } }
    channels { index: 0 drive: STEP_DIR step_dir { max_pulse_rate_hz: 20000 } }
    channels { index: 1 drive: STEP_DIR step_dir { max_pulse_rate_hz: 20000 } }
    firmware { name: "arduino-eth" min_proto_version: 1 }
  }
  boards {
    name: "arm_bus"
    board_type: FEETECH_BUS
    comm { comm_type: SERIAL serial_config { port: "/dev/ttyUSB0" baudrate: 1000000 } }
    channels { index: 0 drive: SERVO_BUS_UART servo_bus { servo_id: 1 } }
    channels { index: 1 drive: SERVO_BUS_UART servo_bus { servo_id: 2 } }
  }
  actions {
    single_actions {
      actuator { motor_type: STEPPER_NEMA17 board_name: "bridge_1" channel: 0 ... }
    }
    single_actions {
      actuator { motor_type: STS3215 board_name: "arm_bus" channel: 0 ... }
    }
  }
}
```

### 6.5 Startup resolution flow

```
 ActionFactory
   │ 1. actuator.board_name → look up Board config
   ▼
 BoardFactory::GetOrCreate("bridge_1")     ── cached: later actuators on the
   │ 2. CommFactory opens Board.comm          same board reuse this instance
   │ 3. IDENTIFY handshake ────────────► firmware replies:
   │                                      "arduino-eth, proto v1,
   │                                       ch0: STEP_DIR, ch1: STEP_DIR"
   │ 4. matches firmware{} + channels{}?  ✔
   │        ✘ → "board reports arduino-serial-v1 but config expects eth
   │             — run tools/flash --board=bridge_1"
   │ 5. push CONFIGURE_CHANNEL tunables
   ▼
 ValidateMotorChannel(motor_type, channel.drive)
   ▼
 board->OpenChannel(0) → BoardChannel
   ▼
 StepperDriver(channel, actuator_config) → registered as the ROS 2 actuator
```

### 6.6 `robot/perception/proto/perception.proto` — unified factory, optional board binding

The action layer migration (§6.3) decouples **what is sensed** from **which
board/transport reaches it**. Perceptions use the **same factory architecture**
as actuators (`PerceptionFactory` + optional `boards{}` resolution), but
**board binding is optional** for sensors — unlike actuators, where it is
always required (§6.6.1).

**Today** (`robot/perception/factory/perception_factory.h`):

```
config .pbtxt
   └─ Encoder { encoder_type, comm{...}, sts3215_encoder_config{...} }
        │
   PerceptionFactory  (no boards{} argument)
        │  switch on EncoderType → picks driver, calls CommFactory
        └─ Sts3215Encoder  ←─ Serial  (CommFactory::CreateSerial(encoder.comm))
```

`ros2/encoder_publisher.cc` calls `PerceptionFactory::CreatePerception` with
no `boards` list. Presets such as
`config/config_preset/so100/encoder_publish.pbtxt` may declare `boards{}` for
the actuator path but encoders still embed per-device `comm {}` — port and
baudrate are duplicated and the encoder bypasses any future board bus mutex
(§5.6).

**Target shape** (mirrors §6.3; names are illustrative until Phase 6 lands):

```proto
enum SensorType {                 // replaces board/transport-flavored EncoderType
  SENSOR_INVALID = 0;
  SENSOR_STS3215_ENCODER = 1;
  SENSOR_LDS01_LIDAR = 2;
  // SENSOR_OPENCV for host-local cameras, or omit board_name when N/A
}

message Encoder {
  string encoder_name = 1;
  uint64 id = 2;
  SensorType sensor_type = 3;
  string board_name = 4;          // optional — set when board-backed (§6.6.1)
  uint32 channel = 5;             // required when board_name is set
  float operational_lower_limit = 6;
  float operational_upper_limit = 7;
  // sensor-specific config (sts3215_encoder_config, …)
  // DEPRECATED during migration: EncoderType encoder_type, robot.comm.Comm comm
}

message Camera {
  string camera_name = 1;
  uint64 id = 2;
  SensorType sensor_type = 3;     // e.g. SENSOR_OPENCV
  // board_name / channel omitted — driver-direct (§6.6.1)
  oneof camera_config { OpenCvConfig opencv_config = 6; }
}
```

`EncoderType::STS3215_ENCODER`, `LidarType::LDS01`, and embedded `Comm` on
board-backed sensors are kept working but deprecated during migration (§10,
Phase 6), then removed. `SensorType` values carry a `SENSOR_` prefix for the
same proto-scope reason as `MotorType` (§6.3).

### 6.6.1 When perceptions skip the board layer

**Rule:** board binding is **mandatory for actuators**, **optional for
perceptions**. Use the board path only when the sensor shares a `Board`
resource with actuators or reads through an MCU channel contract. Do **not**
invent placeholder `boards{}` entries (e.g. `board_type: NONE`) just to keep
config symmetric — that adds noise without runtime benefit.

**Decision guide:**

| Question | If yes → | If no → |
| --- | --- | --- |
| Does the sensor share a half-duplex bus with actuators? | `board_name` + `channel` | driver-direct |
| Does feedback come from an EtherCAT/CAN PDO slot shared with motors? | `board_name` + `channel` | driver-direct |
| Does an MCU aggregate several sensor channels behind one comm leg? | `board_name` + `channel` | driver-direct |
| Is it a standalone smart peripheral (USB camera, Ethernet lidar)? | driver-direct | — |

**Automotive vs humanoid / manipulator robotics** — the same rule applies;
only the wiring mix changes:

| Domain | Typical driver-direct (no board) | Typical board-backed |
| --- | --- | --- |
| Automotive (Thor / central compute) | GMSL/Ethernet cameras, radar, spinning lidar stream to SoC | Wheel/joint feedback on vehicle fieldbus when co-scheduled with actuators |
| Humanoid head / torso | USB/GMSL cameras, IMU on SBC, Ethernet lidar on base | Rare on head sensors; more common on arm/hand fieldbuses |
| Low-cost arm (so100) | — (no standalone head sensors in preset) | Feetech encoders + servos on **one** UART → `arm_bus` |

In all cases the main compute **can** process cameras and lidars directly —
no Joshua `Board` is required when the sensor firmware (or kernel driver)
owns the link and nothing else contends for it. The board layer exists for
**shared-controller** I/O, not for every sensor on the robot.

**Factory resolution** — one entry point, two paths (mirrors how degenerate
boards like `FEETECH_BUS` collapse comm+drive without a separate MCU):

```
 PerceptionFactory::CreatePerception(perception, boards)
        │
        ├─ board_name set?
        │     └─ board path: lookup boards{} → BoardFactory → OpenChannel
        │        → ReadFeedback()  (Sts3215Encoder on Feetech, future PDO encoders)
        │
        └─ board_name empty?
              └─ driver-direct: CvCamera, Ethernet lidar SDK, mock drivers
                 (device path / IP in sensor-specific config, not boards{})
```

**Explicit non-goals:**

- Do not route OpenCV/V4L2 cameras through `boards{}` unless a future board
  type genuinely mediates the link.
- Do not keep a wholly separate perception stack with no board path — encoders
  on shared buses must reuse the same `BoardFactory` cache and mutex as
  actuators (§5.6).

**Sensor attach summary:**

| Sensor | Board layer? | Rationale |
| --- | --- | --- |
| OpenCV / V4L2 camera | **No** — driver-direct | Host-local device; `opencv_config` holds device path |
| Ethernet lidar (Velodyne, Ouster, …) | **No** — driver-direct | Smart peripheral; vendor protocol to SoC |
| Mock encoder / mock lidar (Python) | N/A | Test doubles stay on the Python factory |
| `Sts3215Encoder` on Feetech bus | **Yes** — required | Shares half-duplex serial with actuators (§5.6) |
| `Lds01Driver` over dedicated USB serial | **Optional** | Board-backed when port/cache/mutex must match actuators; driver-direct acceptable on an exclusive port until Phase 6 |

> **TODO(hmoon):** Perception is behind the action layer in the rollout.
> Phase 4 co-migrates only `Sts3215Encoder` onto the actuator's
> `FeetechBusBoard` (bus lock / `ReadFeedback()`). Phase 6 unifies
> `PerceptionFactory` with the optional-board model above — not a forced
> board binding for every sensor. Until then, encoder presets on shared buses
> keep embedded `comm {}` even when `boards{}` is already present for
> actuators.

### 6.7 Example presets — board-backed encoder vs driver-direct camera

Actuators and encoders on the same daisy chain reference the **same**
`boards{}` entry; transport is declared once on the board. A head camera on
the same robot omits `board_name` entirely:

```proto
robot {
  boards {
    name: "arm_bus"
    board_type: FEETECH_BUS
    comm { comm_type: SERIAL serial_config { port: "/dev/ttyACM0" baudrate: 1000000 } }
    channels { index: 1 drive: SERVO_BUS_UART servo_bus { servo_id: 1 } }
    channels { index: 2 drive: SERVO_BUS_UART servo_bus { servo_id: 2 } }
  }
  actions {
    single_actions {
      actuator {
        motor_type: MOTOR_STS3215
        board_name: "arm_bus"       # mandatory for actuators
        channel: 1
        sts3215_config { servo_id: 1 ... }
      }
    }
  }
  perceptions {
    single_perceptions {
      encoder {
        encoder_name: "sts3215_encoder_1"
        sensor_type: SENSOR_STS3215_ENCODER
        board_name: "arm_bus"       # required — shares UART with actuators
        channel: 1
        operational_lower_limit: 1147
        operational_upper_limit: 3154
      }
    }
    single_perceptions {
      camera {
        camera_name: "head_rgb"
        sensor_type: SENSOR_OPENCV
        # no board_name — driver-direct to host (§6.6.1)
        opencv_config { id: 0 width: 1920 height: 1080 fps: 30 fourcc: "MJPG" }
      }
    }
  }
}
```

Dual-port robots (leader on `/dev/ttyACM1`, follower on `/dev/ttyACM0`) use
two `boards{}` entries (`leader_bus`, `follower_bus`); actuators and
encoders on each arm set `board_name` accordingly. `serial_config.id` on the
old embedded `comm {}` is config bookkeeping only — the runtime keys serial
ports by `(port, baudrate)` today.

### 6.8 Perception startup resolution flow

```
 PerceptionFactory::CreatePerception(perception, boards)
        │
        ├─ board_name empty?  ──► driver-direct path
        │       CvCamera / vendor lidar driver / Python mocks
        │       (device path or IP in sensor-specific config)
        │
        └─ board_name set?  ──► board-backed path
                │ 1. look up Board config (same boards{} as actuators)
                ▼
            BoardFactory::GetOrCreate(name)   ── cached with actuators
                │ 2. reuse board instance + bus mutex
                ▼
            ValidateSensorChannel(sensor_type, channel.drive)
                ▼
            board->OpenChannel(n) → BoardChannel
                ▼
            Sts3215Encoder(channel, config)  via ReadFeedback()
```

**Transitional Phase 4** (before §6.6 proto lands): `Sts3215Encoder` may
attach through the board's bus lock while still using the legacy register-read
implementation — the requirement is that **no** perception code opens a
second `Serial` on a port the board already owns.

**Open design** (§12.2): for cyclic boards (EtherCAT/AM243), should encoder
publishers poll `ReadFeedback()` on each timer tick, or subscribe to a
latest-feedback cache updated by the board cyclic loop?

## 7. Firmware architecture

### 7.1 The rule that holds everything together

```
 changes when you REWIRE      →  firmware binary variant   (flash once)
 changes when you re-VERSION  →  shared protocol codec     (both sides, same repo)
 changes when you EXPERIMENT  →  host .pbtxt config        (every run, no reflash)
```

Firmware exposes "N channels of given interfaces speaking proto vX" and
nothing else. No joint names, no limits, no robot identity in the binary.
**Never generate per-robot firmware builds.**

### 7.2 Canonical channel protocol (`joshua_wire_v1`)

Named "wire" deliberately: in this repo "proto" means protobuf, and this is
not protobuf — it is a hand-written wire-format library (framing + CRC +
command encode/decode, in the spirit of MAVLink) that defines the bytes
exchanged between host and MCU.

One C implementation in `firmware/common/`, compiled into both the host board
classes and the MCU firmware — same repo, so the two sides cannot drift
silently. Frame format:

```
[0xA5 sync][len][proto_ver][cmd][channel][payload...][crc16]

cmd: IDENTIFY           → board_type, fw_name, proto_ver, n_channels,
                          per-channel drive + capability bits
     CONFIGURE_CHANNEL  → push Channel.drive_config tunables
     SET_TARGET         → mode(pos/vel/torque) + float32 value
     GET_FEEDBACK       → position, velocity, fault flags
     ENABLE / DISABLE / ESTOP
```

The frame is transport-agnostic: identical bytes over serial, UDP, or SPI.
EtherCAT is the one exception — cyclic PDO images instead of request/response
frames — so EtherCAT boards use a PDO codec that maps the same logical fields
(target mode/value per channel, feedback per channel) into the image layout.
Where multiple boards run Joshua firmware over EtherCAT (AM243, Teensy),
they should share one canonical PDO layout so the host codec is written
once.

### 7.3 How the codec, protos, host orchestration, and drive backends are shared

Four different artifacts are "shared", in four different ways:

```
 ① protobuf (.proto files)   → shared across HOST components only (config language)
 ② wire codec (plain C)      → shared between HOST and every FIRMWARE (byte language)
 ③ host orchestration (C++)  → shared across every HOST board class (JoshuaWireBoard)
 ④ drive backends (C/C++)    → shared across every FIRMWARE with that channel drive
                               (backend_stepdir.{h,cpp}), never touches the host
```

**Protobuf never crosses the wire to the MCU.** Protobuf is the config
language — right on Linux, wrong for an ATmega328 (code size, heap, varint
parsing inside a control loop). The host board class is the translator: it
reads `Channel.drive_config` (protobuf) and emits `CONFIGURE_CHANNEL`
frames (C codec). Firmware never links protobuf; the host never touches raw
frame bytes outside the codec.

```
                .pbtxt config (protobuf ①)
                       │  host-only world
                       ▼
        ArduinoBoard / TeensyBoard / …    ◄── two identity hooks each
                       │  subclasses JoshuaWireBoard  (C++ ③, below)
                       ▼
              JoshuaWireBoard  (Init/IDENTIFY/CONFIGURE_CHANNEL/channels)
                       │  proto fields → fixed C frames
                       ▼
              joshua_wire_v1  (plain C ②)          ◄── THE shared artifact
                       │  raw bytes on the wire
                       ▼
              MCU firmware (compiled with the same joshua_wire_v1)
```

**② is shared by compiling one source file into every binary that touches
the wire** — single source, multiple builds:

```
 firmware/common/joshua_wire_v1.{h,c}      ◄── one source of truth
        │
        ├── Bazel cc_library //firmware/common:joshua_wire_v1
        │         ├─► linked into ArduinoBoard   (host, x86)
        │         └─► linked into TeensyBoard    (host, x86)
        │
        ├── PlatformIO lib in firmware/arduino/  (AVR build)
        └── PlatformIO lib in firmware/teensy/    (ARM build)
```

For that to work the codec must be lowest-common-denominator C: no malloc,
no libc beyond `stdint`, explicit little-endian byte packing, pure
encode/decode functions over caller-provided buffers:

```c
// firmware/common/joshua_wire_v1.h — sketch
int jw1_encode_set_target(uint8_t* buf, size_t cap, uint8_t channel,
                          jw1_mode_t mode, float value);   // → frame length or -1
int jw1_decode(const uint8_t* buf, size_t len, jw1_frame_t* out);  // sync+crc+version
```

Host and firmware call the *same functions*; N boards diverge only in their
**backend** (what a decoded command does: step/dir pulses vs PWM vs FOC) and
their **transport module** (how bytes arrive). Everything between — sync,
framing, CRC, command IDs, field packing — is one compiled-twice file, which
is why the host needs zero per-board protocol code for Joshua-firmware
boards.

Three guards keep the sharing safe:

1. **Same repo, same commit** — host and firmware build from one revision;
   the codec cannot drift silently.
2. **`proto_ver` in every frame + IDENTIFY handshake** — a v1 host rejects a
   stale v2-flashed board at `Init()`, not mid-motion.
3. **Golden-bytes unit test** — encode each command, assert exact byte
   sequences on host CI; any wire-format change fails visibly and forces a
   version bump.

**EtherCAT variant: shared layout instead of shared functions.** Cyclic
transports exchange a memory image, not frames, so the shared artifact is a
packed-struct layout definition:

```c
// firmware/common/joshua_pdo_v1.h — canonical per-channel PDO slots
typedef struct __attribute__((packed)) {
  uint8_t  mode;       // jw1_mode_t
  int32_t  target;     // native units
  uint16_t sequence;
} jw1_pdo_out_channel_t;   // host writes, firmware reads

typedef struct __attribute__((packed)) {
  int32_t  position;
  int16_t  velocity;
  uint16_t fault_flags;
} jw1_pdo_in_channel_t;    // firmware writes, host reads
```

Every board that adopts this canonical layout (AM243 actuator firmware,
future Teensy EtherCAT firmware) is covered by **one** host-side PDO codec — the
motivation behind open question §12.4. Vendor-controlled protocols are the
exception and get their own host-side codec each: the TI demo byte-walk
(`am243_pdo_codec`, kept because we do not control the TI demo firmware) and
the Feetech register protocol (`FeetechBusBoard`).

**① is ordinary host-side reuse:** `board.proto` is imported by
`robot.proto`, compiled once by Bazel, and consumed by every host component
(`BoardFactory`, `ActionFactory`, validation tests, future Python parity).
Firmware's only contact with proto-derived data is indirect — the values the
host copies out of `Channel.drive_config` into `CONFIGURE_CHANNEL`
frames.

**③ is `JoshuaWireBoard` (`robot/board/joshua_wire/`), an abstract
`BoardInterface`.** ② means the wire *bytes* can't drift between host and
firmware; ③ means the host-side *code that drives those bytes* isn't
reimplemented per board either. Everything in `Init()` — open a transport,
run the IDENTIFY handshake (board_id, `proto_ver`, per-channel drive all
cross-checked against config, §7.5), push `CONFIGURE_CHANNEL` for every
channel — and every channel's `Enable`/`Disable`/`SetTarget`/`ReadFeedback`
framing, is identical for any board speaking `joshua_wire_v1` over a
`FrameTransport`. A concrete board (`TeensyBoard` today) subclasses
`JoshuaWireBoard` and supplies only two facts that actually differ per
board — the `robot.board.BoardType` it accepts and the `jw1_board_id_t`
IDENTIFY must report — as **constructor arguments**, not virtual
overrides: identity is compile-time-known data with no logic behind it,
so there's nothing for a vtable entry to buy here (unlike
`ValidateComm`/`CreateTransport`, genuine behavior hooks a
future board might need to override, which stay virtual with sensible
`SERIAL`/`CommFactory::CreateSerial` defaults — what every board on this
class uses today). `ArduinoBoard` is that subclass: identity is still a
one-line constructor, plus a `CreateTransport()` override that waits out
the Uno's DTR auto-reset — not a reimplementation of IDENTIFY /
CONFIGURE_CHANNEL / channel dispatch. See
`robot/board/arduino/arduino_board.h`.

Extracted after `TeensyBoard` first landed, not designed in from the start
— worth calling out because it's the concrete lesson: the moment a second
board on the same wire protocol is genuinely imminent (Arduino, tracked in
this same phase) is the right time to pull the shared 90% out, *before* a
second hand-written copy exists to un-diverge later. `JoshuaWireBoard`'s
own tests (`joshua_wire_board_test.cc`) exercise this generic behavior
through a minimal test-only subclass identifying as `ARDUINO_UNO`, not
`TEENSY41` — deliberately, so passing tests prove the base actually works
for a board other than the one it was extracted from, not just that the
refactor didn't break Teensy.

**④ is `backend_stepdir.{h,cpp}` (`firmware/common/`), firmware-only —
never linked into the host.** Where ② asks "do host and firmware agree on
the bytes" and ③ asks "does every host board class reimplement the same
orchestration," ④ asks the mirror question on the firmware side: does
every firmware image reimplement the same drive-backend logic. It doesn't
have to, for the same underlying reason ② can be one file for every
firmware: STEP/DIR/ENA pulse generation is a physical fact about the
*driver chip* being controlled, not a fact about which MCU is doing the
controlling — `digitalWrite`/`pinMode`/`delayMicroseconds`/`micros()` are
standard Arduino-framework calls that behave identically on Teensy, an
AVR Uno, or an ESP32. (This does **not** generalize to every drive type:
a PWM backend for real motor-control PWM — center-aligned, complementary
outputs, hardware dead-time — would need a different implementation per
vendor's timer peripheral API, so it would be shared as an *interface*
across firmware, not a single source file, the same way `FrameTransport`
is shared as an interface with per-transport implementations rather than
one universal transport. CAN is similar: shared in shape, not in source,
and not even available on every MCU.) The one place this
sharing isn't free: `backend_stepdir.h` depends on `ChannelState` from
`channel_table.h`, which stays per-firmware-image (channel *count* is a
compile-time, per-image fact, §7.5) — so `platformio.ini` needs an
explicit `-I src` in `build_flags`, since PlatformIO does not
automatically expose a project's own `src/` to a `lib_deps` library's
compilation scope the way it does within the project itself.

| Artifact | Language | Shared by | Crosses the wire? |
| --- | --- | --- | --- |
| `board.proto`, `action.proto` | protobuf | host components only | no — config only |
| `joshua_wire_v1.c` | plain C | host boards + all frame-based firmware | **defines** the wire bytes |
| `joshua_wire_board.{h,cc}` | C++ | every joshua_wire_v1 host board class | no — host-only orchestration |
| `backend_stepdir.{h,cpp}` | Arduino-framework C++ | every firmware with a STEP_DIR channel | no — firmware-only, acts on decoded bytes |
| `joshua_pdo_v1.h` | packed C structs | host + all Joshua EtherCAT firmware | **defines** the PDO image |
| Feetech / TI-demo codecs | C++ host-side only | one vendor board each | speaks the vendor's format |

Rejected alternative: nanopb could put protobuf on the MCU wire, unifying ①
and ② — not worth it (varint decode in the control loop, nondeterministic
frame sizes, and EtherCAT needs fixed layouts anyway). Fixed C frames are
simpler and testable to the byte.

### 7.4 Firmware source layout and build variants

**Revised**: `backend_stepdir.cpp` originally shown here under
`firmware/arduino/` moved to `firmware/common/` once a real second board
made the sharing concrete (§7.3 ④) — it's pulled in via `lib_deps`
alongside `joshua_wire_v1`, not copied per firmware. What's still
genuinely per-firmware is shown below: `main.cpp`'s dispatch loop and the
transport modules (byte-level I/O is the one thing that's actually
different per board/variant).

```
firmware/
  common/
    joshua_wire_v1.h/.c        # shared with host
    backend_stepdir.h/.cpp     # shared across every firmware with a STEP_DIR
                                 # channel — MCU-vendor-agnostic (§7.3 ④)
  arduino/
    main.cpp                    # loop { transport_poll(); dispatch(); step_service(); }
    channel_table.c/.h          # per-firmware-image: channel count, ChannelState shape
    transport_serial.cpp
    transport_w5500.cpp         # Ethernet shield (UDP)
    transport_spi_slave.cpp
    platformio.ini
```

```ini
; one env per wiring variant → one artifact each
[env:arduino-serial]     build_flags = -DJOSHUA_TRANSPORT_SERIAL
[env:arduino-eth-w5500]  build_flags = -DJOSHUA_TRANSPORT_W5500
[env:arduino-spi]        build_flags = -DJOSHUA_TRANSPORT_SPI_SLAVE
```

Transport is a **compile-time variant**, producing unambiguous artifacts
(`arduino-eth-v1.hex`). Prefer variants over a fat binary with runtime
transport selection: small MCUs lack the flash/RAM for unused stacks, and the
artifact name states exactly what is on the board. Keep the variant model
even on large MCUs (AM243) for uniformity.

### 7.5 The channel table: pins are host-configured

**Revised from the original design.** This section originally specified
pin numbers as a compile-time-only fact, baked into a per-firmware-variant
C array and never expressible in host config (the one stated exception was
`HOST_GPIO`, which has no separate firmware to bake facts into). Real
usage on the first Joshua-firmware board (Teensy 4.1, §10 Phase 5)
surfaced that this made Teensy/Arduino-style boards the *outlier*, not the
norm: `Am243Config`'s PDO offsets, `ServoBusConfig.servo_id`, and
`HostGpioConfig.pins` all already put their hardware-addressing fact in
host config, not firmware. Pins now follow the same pattern — see
`StepDirConfig` in `robot/board/proto/board.proto`.

The **channel table** now declares only what's genuinely a compile-time
fact — how many channel slots a firmware image exposes, and (for boards
with a fixed single backend per image, as today's firmware is) what drive
type each slot is:

```c
// firmware/teensy/41/src/channel_table.c
ChannelState g_channels[1];  // one STEP_DIR channel slot; no pins here
```

Pin numbers are instead a `StepDirConfig` field, pushed to firmware per
channel via `CONFIGURE_CHANNEL` at `Init()`, applied at runtime
(`pinMode()` et al. run when the command arrives, not at boot):

```proto
channels {
  index: 0
  drive: STEP_DIR
  step_dir {
    step_pin: 2
    dir_pin: 3
    enable_pin: 4
    max_pulse_rate_hz: 4000
  }
}
```

A channel rejects `Enable`/`SetTarget` until its first `CONFIGURE_CHANNEL`
arrives — there's no default pin to fall back on, and no firmware image
to "just work" without a matching config.

This is still a **pinout contract** between the person wiring the robot
and the person writing config — the contract just now lives in the proto
schema and the config file, not a hand-written C array:

1. The firmware variant documents its channel *count* and *drive type*
   per slot ("1 channel, STEP_DIR") — this part is still compile-time,
   verified by `IDENTIFY`.
2. Wiring plugs each motor into whatever pins the person wiring it chooses.
3. Config declares both the binding (`board_name` + `channel`) *and* the
   actual pin numbers used, in one place — `channels { index: 0 step_dir {
   step_pin: 2 ... } }`.

What is checked vs. trusted — narrower than before, but the same
fundamental limit: `IDENTIFY` still verifies the *logical* contract —
protocol version, and each declared channel's index/drive type against
what the firmware actually reports (`n_channels` +
`channel_drives[]`) — so a config/firmware drift (wrong channel count,
wrong drive type per slot) still fails at `Init`. What `IDENTIFY` still
cannot verify is the *physical* end — a servo actually plugged into the
pin config claims it's plugged into. That was true when pins lived in
firmware too (a channel-table typo was just as unverifiable as a pbtxt
typo); moving pins into config doesn't change what's fundamentally
checkable, it changes *who* can make the mistake and *how expensive* it is
to fix — a wrong pin number is now a config edit + rerun, not a
channel-table edit + reflash.

**Why there's no `firmware.name` at all.** Early on this repo had exactly
one Teensy firmware image, so a single hardcoded name
(`"teensy-stepdir"`) equality-checked against config looked like real
verification. It doesn't scale: the moment a second firmware variant for
the same board exists (more channels, a different backend, PWM instead of
STEP_DIR), each needs its own hand-picked literal, hand-kept in sync
between the firmware source and every pbtxt that targets it, with no
registry enforcing uniqueness or meaning. Worse, it duplicates
`board_type` without adding a principled second axis — it's an arbitrary
password two files have to agree on, not a description of what the
firmware can actually do. The capability check above (`n_channels` +
`channel_drives[]` vs. the config's declared `channels[]`) is the real,
structural, self-scaling version of "is this the firmware I think it is":
it validates facts the firmware already reports, generalizes to any
board/firmware combination with zero per-image coordination, and doesn't
regress when firmware variants multiply. So `FirmwareSpec.name` was
removed outright (`reserved 1; reserved "name";` in the proto) rather than
kept as a dead/advisory field — a field nobody reads or is required to
populate is worse than no field, since it invites someone to reintroduce
exactly this non-scaling check later. The wire protocol's IDENTIFY
response still carries an `fw_name` byte array (`joshua_wire_v1.h`,
shared by every board on this codec) — Teensy's firmware just leaves it
zeroed, since removing it from the wire format itself would be a breaking
change to a codec every future board also speaks, for a field nothing
reads.

Change cadence, updated: rewiring a motor to a different pin is now a
**pbtxt edit with the firmware untouched** — the same cadence as retuning a
motor's speed or direction, since both are `StepDirConfig` fields now.
What still requires a firmware rebuild + reflash: adding an entirely new
channel *slot*, or changing which backend (drive type) a slot compiles
against. The channel table's role shrank to exactly that boundary.

### 7.6 Flash tooling

`tools/flash/` reads the same robot config, resolves each board's needed
artifact from a firmware manifest, and invokes the right flasher:

```
bazel run //tools/flash -- --config=so100/teleop.pbtxt --board=bridge_1
  → board_type=ARDUINO_UNO, comm=ETHERNET_UDP → arduino-eth-v1
  → avrdude -p m328p -c arduino -U flash:w:arduino-eth-v1.hex
```

Manifest maps `(board_type, transport) → artifact + flash method` (avrdude
for Arduino, `teensy_loader_cli` for Teensy, TI UniFlash/OpenOCD for AM243).
Flashing stays **out of the runtime path**, consistent with
`am243_ethercat.md`. The runtime's role is only to *detect* mismatch via the
handshake and name the fix. Auto-flash on boot is rejected: bricking
mid-deploy, and reflashing one EtherCAT node takes the whole bus down.

## 8. Execution flows

Per-command path, frame-based board:

```
 StepperDriver.SetPosition(90°)          host: limits check, deg→steps
   └► channel_->SetTarget(kPosition, 4500)
        └► joshua_wire_v1 encode → comm_->Send → wire → MCU decode
             └► dispatcher → backend_stepdir pulses TB6600 → motor moves
                  └► ACK frame → Status OK (or timeout → error)
```

Per-command path, cyclic board (EtherCAT/AM243):

```
 SetTarget stages value in output image (no I/O)
 board cyclic loop @ fixed rate:
   encode all channels → ExchangeProcessData() → check WKC → decode feedback
```

Transport swap (serial → UDP on the same Arduino): edit `Board.comm`, flash
the matching firmware variant once, run. Zero host code changes. Board swap
(Arduino → AM243 for the same stepper): edit `boards{}` + `board_name`,
zero motor-driver changes.

## 9. Risks & mitigations

| Risk | Mitigation |
| --- | --- |
| Proto migration breaks existing presets | Phase 1 keeps deprecated fields working; presets migrate in Phase 4; validation test (`config_preset_validation_test`) covers both during transition |
| Cyclic-loop threading bugs (staged image vs loop) | Single writer (loop) for wire I/O; channel staging behind a mutex or lock-free slots; reuse existing `ethercat_status` WKC validation |
| Codec drift between host and firmware | One shared C source in-repo; proto_ver in every frame; handshake rejects version mismatch |
| ACK round-trip too slow on chatty buses | Frame protocol allows fire-and-forget mode per command class later; measure first |
| STS3215 refactor regresses working so100 robots | Port behind the new layer with byte-identical bus traffic; keep `test_sts3215_encoder.py` and teleop presets as regression gates |
| Encoder/actuator bus race on shared Feetech UART | Still open after Phase 4: `FeetechBusBoard` lands with its own bus mutex, but `Sts3215Encoder` does not yet route through it (needs `PerceptionFactory` to take `boards`, §10 Phase 6); Phase 6 removes per-encoder `comm {}` from presets |
| AM243 real firmware PDO layout unknown | Demo codec stays isolated behind `Am243PdoMapping` exactly as today; board layer does not depend on the final layout |
| Small-MCU RAM/flash limits (ATmega328 + W5500) | Codec is dependency-free C; per-variant builds strip unused transports; Teensy/ESP32 as fallback targets |
| Userspace GPIO step generation jitters (HOST_GPIO) | Restrict backends to hardware PWM / RP1 / kernel helper; cap `max_pulse_rate_hz` in validation until an RT backend exists (§12.8) |

## 10. Phased rollout (TODO)

Each phase lands green and independently revertible.

### Phase 1 — Proto groundwork
- [x] Add `robot/board/proto/board.proto` (`Board`, `Channel`,
      `DriveInterface`, `FirmwareSpec`, `BoardType`).
- [x] Add `repeated robot.board.Board boards` to `config/proto/robot.proto`.
- [x] Add `MotorType`, `board_name`, `channel` to `Actuator`; mark
      `ActuatorType` board-flavored values and embedded `comm` deprecated
      (still functional).
- [x] Extend `comm.proto` with `ETHERNET_UDP` (+ `UdpConfig`); SPI deferred
      until an SBC host needs it. `CommType.BLE` exists with no config
      message, as scoped — `BleConfig` lands with the Spike/Python
      migration (now §10 Phase 9).
- [ ] Add `ROS2` (+ `Ros2Config` with namespace, domain id, and discovery
      timeout) to `comm.proto` for vendor topic-backed boards (§5.7). Not
      started — needed by §10 Phase 8 (`ROS2_VENDOR_ROBOT`), not before.
- [x] Update `config_preset_validation_test` for both old and new shapes —
      the generic all-`.pbtxt` parse test covers both, since old-shape
      (`actuator_type`) mock/Spike presets and new-shape (`board_name`)
      presets both exist under `config/config_preset/` today.

### Phase 2 — Board layer skeleton
- [x] `robot/board/interfaces/`: `BoardInterface`, `BoardChannel`,
      `TargetMode`, `ChannelFeedback`.
- [x] `robot/board/factory/`: `BoardFactory` with instance cache keyed by
      board name.
- [x] `ValidateMotorChannel(motor_type, drive)` compatibility
      table + tests.
- [ ] Move the serial `PortResources` cache out of `comm_factory.cc` behind
      the board factory. Not done — `PortResources` still lives in
      `robot/comm/factory/comm_factory.cc`; only the EtherCAT transport
      cache moved (Phase 3 below).

### Phase 3 — Port AM243
- [x] EtherCAT transport cache in `CommFactory` keyed by `interface_name` —
      one SOEM master per NIC, mirroring the serial `PortResources` cache
      (today every call constructs a new master; two `ecx_init()`s on one
      NIC fight over the raw socket).
- [x] `robot/board/am243/Am243Board`: absorbs split LRD/LWR validation
      (currently in `ActionFactory`), PDO region mapping, and WKC checks;
      the cyclic exchange loop lives with the shared master (§5.3/§5.8).
- [x] Board init owns the full SOEM lifecycle — `Init → ConfigureSlaves →
      StartCyclic → verify OPERATIONAL`, covered by
      `Am243BoardTest.InitOwnsFullSoemLifecycle`.
- [x] Move `am243_pdo_codec.*` to `robot/board/am243/`; keep
      `AM243_PDO_MAPPING_TI_DEMO` isolation.
- [x] Replace `Am243EthercatDriver` with a thin motor driver over
      `BoardChannel` (`TiDemoDriver` — deliberately demo-scoped, not the
      generic joint driver; it retires with `MOTOR_TI_DEMO`).
- [x] Rewire `am243_demo_smoke`, `am243_driver_smoke`, `am243_config_smoke`
      to the new path — these are the hardware regression gates.
- [x] Prove the config-driven path end to end:
      `am243_ethercat_demo.pbtxt → actuator_subscriber → Am243Board` drives
      the TI demo. `ActionFactory` routes `MOTOR_TI_DEMO` through
      `BoardFactory → Am243Board → TiDemoDriver` and this is unit-tested
      (`ActionFactoryBoardPathTest.CreatesTiDemoDriverOverMockBoardChannel`)
      — same caveat as Phase 4's `FeetechBusBoard`: proven in software over
      a mock board, not yet run against real LP-AM243 hardware from the
      config-driven path end to end.
- [x] Update `docs/am243_ethercat.md` boundaries section.

### Phase 4 — Port STS3215 (actuators + encoder co-migration)

**Actuators:**
- [x] `robot/board/feetech_bus/FeetechBusBoard`: Feetech register protocol
      (`feetech_protocol.h`, pure/unit-tested), one instance per board name
      (via `BoardFactory`'s existing name cache — a config that opens two
      `FEETECH_BUS` boards on the same port is a config error, same as
      today's serial `PortResources` cache), bus mutex, servo-ping +
      model-number-read IDENTIFY. Register addresses match the public
      STS3215 memory map; unverified against real hardware on this branch
      (no LP servo bus wired up here — same caveat as PR #67's pending
      AM243 hardware smoke run).
- [x] Slim `Sts3215Driver` to motor semantics over `BoardChannel`. Every
      write (torque enable, the bundled position+time+speed burst, present-
      position read) is byte-identical to the pre-board-layer driver's wire
      traffic for the same inputs (verified in `feetech_protocol_test.cc`
      against hand-computed legacy checksums). The bundled burst is built by
      the channel from a driver-staged speed (`SetTarget(kVelocity, ...)`,
      mirrors the old driver's SetSpeed, which also only updated local state
      with no immediate write) plus a board-config move-time tunable
      (`ServoBusConfig.move_time_ms`, pushed once at `Init()`, matching the
      `Channel` proto's own CONFIGURE_CHANNEL framing) — two `SetTarget`
      calls instead of one API bundling all three fields, since
      `BoardChannel::SetTarget` is single-value, but the same bytes land on
      the wire.
- [x] Decide the torque mapping (docs/BOARD_LAYER_RFC.md §12.7, resolved in
      `robot/board/interfaces/board_channel.h`): `Enable`/`Disable` is the
      on/off gate for boards whose torque is fundamentally a binary enable
      register; `TargetMode::kTorque` is reserved for boards with a genuine
      continuous torque target. `Sts3215Driver::SetTorque` now calls
      `Enable()`/`Disable()`. The AM243 TI demo firmware has neither a real
      enable register nor a real continuous torque target — it stays on its
      existing target-scaled placeholder, documented in
      `Am243DemoChannel`, and is expected to retire with `MOTOR_TI_DEMO`
      rather than adopt the new rule.
- [x] Migrate so100 **actuator** presets to `boards{}` + `board_name` (no
      per-actuator `comm {}`); keep teleop working.
- [x] Remove deprecated C++ `ActionFactory` `actuator_type` arms; route all
      actuators through `motor_type` + `board_name` + `channel`.
- [ ] Remove deprecated `ActuatorType` values and embedded `Actuator.comm`
      from proto once nothing switches on them. No longer blocked by
      `action_factory.py` — §10 Phase 9 deleted the Python factory. The
      remaining users are the C++ `ActionFactory`'s deprecation check and
      the AM243 preset.

**Perception (partial — bus safety only; full migration is Phase 6):**
- [ ] Route `Sts3215Encoder` (perception) through `FeetechBusBoard` — or at
      minimum through the board's bus lock. It currently writes raw Feetech
      frames to the same shared `Serial` from encoder-publisher timers,
      which would bypass the new bus mutex (§5.6, §6.8). **Not done in the
      FeetechBusBoard port**: doing this properly needs `PerceptionFactory`
      to receive `boards` (the same signature change §10 Phase 6 already
      lists as its own deliverable), not just a `Sts3215Encoder` constructor
      swap — so it's left for Phase 6 rather than half-done here. The known
      bus-race risk in the table above (§9) is unchanged by this PR.
- [ ] Regression: `encoder_publish` + teleop on so100 with actuators and
      encoders on the same port — no half-duplex bus collisions. Blocked on
      the item above.

### Phase 5 — Prove the matrix (first real second board)

First real second board is **Teensy 4.1**, not the placeholder Arduino
this section originally sketched — chosen because real Teensy 4.1 + TB6600
hardware exists to bring this up on, whereas the Arduino sketch was
speculative. The architecture is identical either way (§7.1–§7.5 make no
Arduino-specific assumption). `ArduinoBoard` + `firmware/arduino/uno/`
are now in tree — same `joshua_wire_v1` codec, same `StepperDriver`, same
`FrameTransport` seam, new AVR firmware image, and (§7.3) a host board
class that's just a one-line constructor subclassing `JoshuaWireBoard`
rather than a from-scratch `BoardInterface` implementation. Host/firmware
source is written; flashing and physical rotation on a real Uno R3 are
still open.

**ESP32** (hardware on hand) got a first cut for the same reason Teensy
did: `firmware/esp32/` joins the exact same joshua_wire_v1 family as
Teensy — plain UART/USB-serial, STEP_DIR, `backend_stepdir.{h,cpp}` reused
unchanged from `firmware/common/`, `Esp32Board`'s constructor identical in
shape to `TeensyBoard`'s. One real difference from Teensy: `Esp32Board`
overrides `CreateTransport()` to add a ~2s settle delay after opening the
port, because most ESP32 dev boards (CP2102/CH340 bridge) reset on serial
open — DTR is wired into EN, the same auto-reset trick `esptool` uses —
so the first IDENTIFY otherwise races the board's own boot log (see
`firmware/esp32/README.md`'s Known gaps for the full mechanism). This is
deliberately the boring, immediately-buildable path, not the Wi-Fi/UDP
transport-swap proof this section originally speculated ESP32 might be
for — that variant is still real, still tracked, and still needs a
`robot::comm::UdpTransport` and a `UdpFrameTransport` that don't exist yet
(§7's TODOs on `frame_transport.h` and `comm_factory.h`), but it's a
separate, larger piece of follow-up work from "ESP32 exists as a board at
all." Flashed and protocol-verified on real hardware (IDENTIFY, ENABLE,
SET_TARGET all confirmed via `esp32_driver_smoke`) — physical motor
rotation not yet observed on this board; see `firmware/esp32/README.md`'s
Status.

- [x] Define `joshua_wire_v1` frame codec in `firmware/common/` (C, shared;
      golden-bytes unit tests on host CI, §7.3). Every response has a
      fixed size for a given `proto_ver` (`JW1_*_RESPONSE_PAYLOAD_LEN` in
      `joshua_wire_v1.h`) — including `IDENTIFY`, whose `channel_drives`
      array is always transmitted at `JW1_MAX_CHANNELS` and padded with
      `JW1_DRIVE_INVALID` — so a host built on a fixed-size-read transport
      (`robot::comm::SerialTransport::AtomicRead`) never needs an
      incremental header-then-body read.
- [x] `firmware/teensy/41/` with the serial (native USB) transport variant;
      `backend_stepdir` (no acceleration ramp yet — constant rate capped
      by `max_pulse_rate_hz`; ramping is unstarted follow-up work once
      there's hardware to tune it against).
- [x] Host side: `TeensyBoard` (`robot/board/teensy/`) + generic
      `StepperDriver` (`robot/action/motors/drivers/`) + `FrameTransport`
      seam (`robot/board/frame/`, `SerialFrameTransport` on
      `robot::comm::SerialTransport`).
- [x] Host + firmware for Arduino Uno R3: `ArduinoBoard`
      (`robot/board/arduino/`), `firmware/arduino/uno/` serial variant,
      example preset `config/config_preset/example/arduino_stepper_demo.pbtxt`.
      First hardware goal is IDENTIFY-only (USB, no motor). Flash and
      handshake on a real Uno are still open; motion is deferred.
- [x] `JoshuaWireBoard` (`robot/board/joshua_wire/`, §7.3) extracted from
      `TeensyBoard` once a second board on this wire protocol (Arduino)
      was concretely imminent: IDENTIFY handshake, `CONFIGURE_CHANNEL`,
      and channel dispatch now live once, shared by every joshua_wire_v1
      board; `TeensyBoard` shrank to a one-line constructor passing its
      identity (`BoardType::TEENSY41`, `JW1_BOARD_TEENSY41`) up to
      `JoshuaWireBoard` — plain constructor data, not a virtual hook,
      since there's no behavior behind it for a vtable entry to buy.
      Also closed a real gap
      found reviewing the extraction: IDENTIFY's `board_id` was being
      decoded and never checked against config — now it is, generically,
      for every board on this class.
- [x] End-to-end smoke: pbtxt → ActionFactory → BoardFactory → frames →
      Teensy → TB6600 → **stepper physically rotates**, verified on real
      hardware through the actual production path (`launcher:joshua_main`
      + `ros2 topic pub`, not just raw protocol scripts). Wiring diagram
      and DIP switch notes folded into
      `firmware/teensy/41/README.md`'s Wiring / Pinout section. Getting
      here surfaced and fixed real bugs no software-only pass could catch
      — build/flash tooling (`platformio.ini`'s platform name, a
      Bazel-vs-PlatformIO include-path mismatch, a missing
      `library.json` `srcFilter`,
      `node_generator.cc`'s `IsCppDriverAvailableForAction` missing
      `MOTOR_STEPPER_NEMA17`), a stale incremental PlatformIO build that
      silently bypassed the firmware's enable-gate, `StepperDriver::Init()`
      never calling `Enable()` (fixed by auto-enabling, matching
      `TiDemoDriver` rather than `Sts3215Driver`, since an open-loop
      stepper's `ENA` pin has no safety case for withholding it), a
      board-specific TB6600 DIP switch layout that didn't match the
      assumed "standard" table, and — the actual root cause of "clicks/
      locks but never rotates" — `ENA-`/`PUL-`/`DIR-` needing to be wired
      to Teensy GND for the opto-isolated inputs to work at all. See the
      bring-up log for the full debugging methodology, worth reusing on
      the next board.
- [ ] Add the UDP (W5500) firmware variant + `UdpTransport` to demonstrate a
      transport swap with zero host-code changes. Not started — the
      `FrameTransport` seam above is exactly what makes this swap
      zero-host-code-change once attempted (§7.3, §7.4).

### Phase 6 — Perception layer parity

Unified `PerceptionFactory` with **optional** board binding (§6.6.1) — same
architecture as actuators, but only board-backed sensors set `board_name`.
Lands after Phase 4 `FeetechBusBoard` exists so encoder co-migration can be
completed cleanly, not just bus-locked.

**Proto & validation:**
- [ ] Add `SensorType`, optional `board_name` + `channel` to `Encoder` (and
      `Lidar` where board-backed); mark `EncoderType` / embedded `comm`
      deprecated on board-backed sensors only.
- [ ] Validation: `board_name` required when sensor shares a bus with
      actuators; forbidden empty `board_name` on Feetech/EtherCAT encoders;
      `board_name` omitted for driver-direct cameras.
- [ ] Add `ValidateSensorChannel(sensor_type, drive)` (+ tests), called only
      on the board-backed path; mirrors `ValidateMotorChannel`.
- [ ] Update `config_preset_validation_test` for both old and new perception
      shapes during transition.

**Runtime:**
- [ ] `PerceptionFactory::CreatePerception(single_perception, boards)` —
      fork: `board_name` set → board path; empty → driver-direct (§6.8).
- [ ] Update `ros2/encoder_publisher.cc` (and `lidar_publisher.cc` when
      applicable) to pass `config.robot().boards()`.
- [ ] Slim `Sts3215Encoder` to sensor semantics over `BoardChannel`
      (`ReadFeedback()`); retire direct `Serial` ownership on shared buses.
- [ ] Route `Lds01Driver` through a board when the lidar shares a transport
      that needs instance caching (optional if deferred; exclusive port may
      stay driver-direct per §6.6.1).

**Presets & node generation:**
- [ ] Migrate so100 **board-backed** perception presets (`encoder_publish`,
      `sim_mirror`, …): remove per-encoder `comm {}`; bind via `board_name` +
      `channel`.
- [ ] Leave camera / Ethernet-lidar presets driver-direct (no `boards{}`
      entry required).
- [ ] Update `node_generator` `IsCppDriverAvailableForPerception` to select
      backend from `sensor_type` and whether the board-backed path applies,
      not legacy `encoder_type` alone.

**Explicit non-goals for this phase:**
- OpenCV cameras stay driver-direct unless a future board type needs them.
- Python mock perception drivers stayed on `perception_factory.py` for this
  phase. Phase 9 deleted them outright rather than porting them to C++, so
  Phase 6 inherits no mock doubles to unify.
- No placeholder `board_type: NONE` or other fake boards for symmetry.

### Phase 7 — Firmware tooling & handshake hardening
- [ ] `IDENTIFY` capability bits + per-channel drive report;
      `CONFIGURE_CHANNEL` tunables push.
- [ ] `FirmwareSpec` verification in every `Board::Init` with actionable
      error text.
- [ ] `tools/flash/` + firmware manifest
      (`(board_type, transport) → artifact + flash method`).
- [ ] Docs: firmware contribution guide (how to add a board / a transport
      variant / a backend).

### Phase 8 — ROS 2 vendor robot board
- [ ] Add `ROS2_VENDOR_ROBOT` board support with graph validation in
      `Board::Init()` instead of firmware IDENTIFY.
- [ ] Add `MobileBase` / `DifferentialDriveBase` action shape and
      `MOBILE_BASE_VELOCITY` channel validation.
- [ ] Implement a generic `Ros2VendorRobotBoard` that publishes configured
      command topics and subscribes to configured feedback topics.
- [ ] Prove with one common platform preset, preferably TurtleBot 3 or
      TurtleBot 4/Create 3: `.pbtxt → ActionFactory → BoardFactory →
      /cmd_vel` plus odometry feedback.
- [ ] Document vendor-specific endpoint presets for TurtleBot, Clearpath
      Jackal/Husky, Stretch, and similar robots.

### Phase 9 — Remove the Python robot-side implementation

Resolves open question §12.3: the board layer replaces the need for a
parallel Python implementation, rather than growing one. **Done — `robot/`
contains no Python at all.** Rather than port each remaining Python path to
C++ first, this phase deleted the Python robot layer outright and accepted
the two capability regressions that follow (mocks and Spike, below).

**Landed:**
- [x] Deleted every `.py` under `robot/`: `action_factory.py`,
      `perception_factory.py`, `comm_factory.py`, the action and perception
      interface base classes, `serial.py`, and the mock drivers
      (`mock_driver.py`, `mock_camera.py`, `mock_encoder.py`,
      `mock_lidar.py`), plus every `py_library`/`py_test` target that
      carried them.
- [x] Moved `pybricks_driver.py` and `pybricks_ble_transport.py` out of
      `robot/` into `tools/pybricks/`, alongside the existing
      `pybricks_ble_smoke` binary. They are bench tooling, not runtime code:
      nothing on the launcher's path reaches them, and they are the working
      reference for the native `SPIKE_HUB_BLE` port. `PybricksMotorDriver`
      no longer implements `ActuatorInterface` (that ABC went with the rest
      of the Python robot layer) but keeps the same method shape.
- [x] Deleted the Python hardware nodes `ros2/actuator_subscriber.py`,
      `camera_publisher.py`, `encoder_publisher.py`, and
      `lidar_publisher.py` with their `ros2_py_binary` targets; the C++
      binaries of the same names are now the only implementations.
- [x] Removed backend selection from `node_generator`. `BackendPreference`,
      `IsCppDriverAvailableForAction`, `IsCppDriverAvailableForPerception`,
      `DeterminePreferredBackend`, and the C++→Python launch fallback are
      gone. Node launch no longer inspects motor or perception types at
      all: every node's executable is named for its node type. The
      vestigial `_py` suffix went too — it existed to tell a Python
      binary apart from a same-named C++ one, and no Python node has a
      C++ counterpart any more, so `//ros2:trajectory_publisher_py` was
      renamed `//ros2:trajectory_publisher` to match `inference` and
      `data_subscriber`.
- [x] Removed the mock actuator path end to end: `MockMotorDriver`,
      `MOTOR_MOCK`/`MOCK_MOTOR`/`MockMotorConfig`, the `MOTOR_MOCK` arm of
      `ValidateMotorChannel`, and `example/mock_py_test.pbtxt`. The mock
      perception types `MOCK_CAMERA`/`MOCK_ENCODER`/`MOCK_LIDAR` went with
      them.

**Kept deliberately:**
- `BoardType::MOCK` (`robot/board/mock/`) is **not** part of the removed
  mock path — it is C++ test infrastructure that lets `ActionFactory`,
  `BoardFactory`, and the AM243/Feetech/JoshuaWire board tests exercise
  *real* drivers with no hardware attached. It stays.
- `scripts/pybricks_spike_bridge.py` runs on the SPIKE hub in MicroPython,
  not on the host. It is firmware, not robot-layer code.
- `tools/pybricks/` is developer tooling, off the runtime path. Keeping it
  costs nothing at runtime and preserves a working Pybricks implementation
  to port from.
- `ros2/trajectory_publisher.py`, `data_subscriber.py`, and `inference.py`
  stay Python: they are ROS/AI plumbing with no hardware access and no C++
  implementation. `node_generator` maps their node types to those targets
  directly.

**Accepted regressions:**
- **Joshua no longer supports SPIKE hubs at all.** This resolves §12.3 by
  dropping the capability rather than porting it. The Python Pybricks path
  went first; then the `SPIKE_HUB_BLE` stub board
  (`robot/board/spike_hub_ble/`), `BoardType::SPIKE_HUB_BLE`, and
  `MotorType::MOTOR_SPIKE` were all removed too, since a stub that only ever
  returned `UnimplementedError` was scaffolding for a port nobody had
  scheduled. `pybricksdev` (BLE scan/connect via Bleak, MicroPython program
  upload, line protocol over notify/write) has no C++ equivalent library and
  validating a native port needs a physical hub, so the port was never
  cheap.
  `python_spike_actuator_example.pbtxt`,
  `python_spike_trajectory_example.pbtxt`, and `docs/pybricks_test.md`
  were deleted rather than left as dead config.
  A SPIKE motor can still be driven **by hand** through
  `//tools/pybricks:pybricks_ble_smoke` — that tool is the whole remaining
  Spike story, and it is self-contained: `ActuatorType::SPIKE_MOTOR`,
  `SpikeMotorConfig`, and `CommType::BLE` were removed from the robot protos
  too, and the tool now configures itself with its own
  `SpikeMotorSpec` dataclass rather than a `robot.action.Actuator`. It keeps
  only the generic `ActionPacket` dependency, which is not Spike-specific.
  `BLE` went because the Spike hub was its sole user.
- There are no mock actuator or mock perception components any more, so no
  preset can exercise the node graph without real hardware. Config
  validation and `bazel test` remain the hardware-free verification path.

**Still open:**
- [ ] Nothing on the Spike side. Re-adding hub support means re-adding a
      board type, a motor type, and a native BLE implementation — a new
      feature, not a pending migration. `tools/pybricks/` is the reference
      if that is ever picked up.
- [ ] Phase 6 (perception layer parity) is unaffected and still pending.
      C++ perception already covers `OPENCV`, `STS3215_ENCODER`, and
      `LDS01`; what Phase 6 adds is board binding for perception, not a
      Python replacement.
- [ ] Remove the deprecated `ActuatorType` values and embedded
      `Actuator.comm` from the proto (deferred from §10 Phase 4). No
      longer blocked by `action_factory.py` — the remaining users are the
      C++ `ActionFactory`'s deprecation check and the AM243 preset.

## 11. Acceptance criteria

- A stepper motor moves via Teensy 4.1 (driving a TB6600) over serial **and**
  over UDP by changing only `Board.comm` and reflashing the matching
  variant — zero host code changes. (Serial: **fully verified on real
  hardware, including physical rotation**, §10 Phase 5. UDP variant: not
  started, same phase. Arduino Uno R3 serial is the second MCU on those
  seams — source in tree, hardware verification still open.)
- Existing so100 teleoperation and AM243 smoke tests pass through the new
  layers with unchanged wire behavior — and the config-driven AM243 path
  (`.pbtxt → actuator_subscriber`) works end to end for the first time.
- Two actuators on one board share one board instance, one comm handle, one
  cyclic loop (verified by test).
- A wrong-firmware or wrong-transport board is rejected at `Init()` with an
  error that names the board and the `tools/flash` command to fix it.
- Adding a hypothetical new board requires: one board class, one firmware
  target, config — and **zero** changes to motor drivers or comm transports
  (demonstrated in review by the Phase 5 diff shape).
- Board-backed encoders on a shared bus (so100 Feetech) use the same board
  instance and bus mutex as actuators on that port — no duplicate `Serial`
  opens; `encoder_publish` + teleop regression passes (§6.8, Phase 6).
- Driver-direct cameras and Ethernet lidars work without any `boards{}` entry;
  `PerceptionFactory` takes the empty-`board_name` path (§6.6.1).

## 12. Open questions

1. **Units at the seam** — native units (steps/ticks) with conversion in the
   motor driver (current proposal), or SI at the seam with per-channel scale
   pushed via `CONFIGURE_CHANNEL`? Decide before Phase 2 freezes
   `BoardChannel`. One refinement is already clear: for *firmware-owned*
   joints the native unit is a firmware fact the host has no motor model
   for, so the eventual generic joint driver (the `ACTUATOR_V1`-era
   replacement for the demo-scoped `TiDemoDriver`) should obtain its scale
   from the channel/board contract — channel metadata or unit config —
   rather than hardcoding a full-scale constant the way `TiDemoDriver`
   hardcodes the TI demo's 0-255 seed byte.
2. **Feedback pull vs push** — `ReadFeedback()` polling is fine for
   frame-based boards; should cyclic boards also expose a subscription/latest
   cache for encoder publishers, and does the perception layer read through
   the same board instance? See §6.8; decide before Phase 6 freezes
   `PerceptionFactory`.
3. **Python parity — DONE**: no parallel Python board-layer
   implementation. §10 Phase 9 deleted `action_factory.py` /
   `perception_factory.py` / `comm_factory.py` and everything under them;
   `robot/` is Python-free. The Pybricks/Spike path was dropped rather than
   ported, and the `SPIKE_HUB_BLE` board type went with it — Spike support
   is gone, not deferred. See `tools/pybricks/` for the bench tool.
4. **Canonical EtherCAT PDO layout** — one shared Joshua PDO schema for AM243
   and future Teensy firmware: fixed 8-byte-per-channel slots, or
   ESI/SDO-described dynamic mapping?
5. **Cyclic loop cadence** — ownership is settled (one loop per NIC, owned
   by the shared master transport; per-board loops are impossible on a
   shared bus, §5.3/§5.8). Still open: cycle rate, thread priority, and
   jitter targets.
6. **ESTOP semantics** — protocol-level broadcast (all channels, all boards)
   and its guarantees on frame-based vs cyclic transports.
7. **Torque semantics at the seam** — `TargetMode::kTorque` as a control
   target vs the STS3215's torque-*enable* register (an on/off that is
   really `Enable/Disable`); pick one mapping rule before Phase 4 ports the
   driver.
8. **HOST_GPIO real-time backend** — which pulse-generation mechanism
   (hardware PWM, Pi 5 RP1, kernel/RT helper) and the max pulse rate to
   allow from a non-RT userspace process.
9. **TODO: firmware build/flash version control.** `FirmwareSpec` today
   only carries `min_proto_version` — the wire *protocol* version, which
   says nothing about which actual build is flashed on a given board.
   `firmware.name` was deliberately removed rather than kept as a
   free-form identity check (§7.5), so there is currently no way to ask a
   live board "which build are you" at all — the class of bug that caused
   (a stale `.pio` build silently diverging from source,
   `firmware/teensy/41/README.md`'s Known gaps section) has no automated
   guard today. Proposed direction: an auto-generated
   build id (`git describe --always --dirty` or short commit SHA) injected
   at compile time via PlatformIO `build_flags`, reported over `IDENTIFY`
   in the now-dormant `fw_name` wire slot — **diagnostic-only, never a
   host-enforced gate**, so it doesn't reintroduce the same non-scaling
   mistake `firmware.name` was. Undecided: exact injection mechanism, and
   whether a `tools/flash` wrapper (§6.1's original sketch) should also log
   what was flashed where/when for fleet-level traceability once more than
   one physical board exists.
