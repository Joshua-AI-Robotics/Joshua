# Board Layer RFC: Motor × Board × Transport Decomposition

Status: Draft (design only — no implementation in this document)
Companion to: [ARCHITECTURE.md](ARCHITECTURE.md), [am243_ethercat.md](am243_ethercat.md)

## 1. Purpose

Make the actuator stack fully configurable across three independent axes:

- **Motor** — what physically moves (STS3215 servo, NEMA17 stepper, Spike
  motor, future BLDC).
- **Board** — the controller that runs the low-level motor loop (AM243,
  Teensy, Arduino + TB6600, or the MCU inside a smart servo).
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
- Proto schema: top-level `boards`, motor-centric `Actuator`, the two comm
  hops, `FirmwareSpec`.
- The canonical host↔firmware channel protocol (`joshua_proto_v1`) and the
  cyclic (EtherCAT/PDO) variant.
- Firmware architecture, build variants, flashing tooling, and the IDENTIFY
  handshake.
- Migration of the existing STS3215 and AM243 paths.

Out of scope (future work):

- Real AM243 actuator firmware and its final PDO layout (tracked separately;
  see `am243_ethercat.md` — only the TI demo mapping is validated today).
- Encoder/perception devices behind boards (the same pattern should apply;
  deferred until an actuator-side board layer exists).
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
| AM243 EtherCAT driver (TI demo PDO only) | ✅ working | `robot/action/motors/drivers/am243_ethercat_driver.*` |
| **Board abstraction (shared controller instances)** | ❌ missing | — |
| **Motor drivers decoupled from transports** | ❌ missing | drivers hold `Serial`/`EthercatTransport` directly |
| **Canonical host↔MCU channel protocol** | ❌ missing | — |
| **Firmware artifacts + flash tooling** | ❌ missing | — |
| **Firmware/config handshake at init** | ❌ missing | — |

Structural problems this RFC fixes:

1. **Combinatorial explosion.** A stepper on TB6600+Arduino over serial, the
   same stepper on AM243 over EtherCAT, and a servo on Teensy+EasyCAT would
   each need a new `ActuatorType`, driver class, and factory arm —
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

## 4. Design goals

- One file per motor type, one per board, one per transport — never one per
  combination.
- Boards are first-class, **shared** config objects; actuators bind to
  `(board_name, channel)`.
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
      ┌──────────────────────────┐          tb6600-arduino-eth-v1.hex
      │  Motor driver            │          am243-ethercat-joshua-v1.bin
      │  "what moves"            │          teensy-ecat-joshua-v1.hex
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
      │  Tb6600ArduinoBoard,     │  joshua_proto │  │
      │  Sts3215BusBoard         │     _v1       │  │
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
      │  (same joshua_proto_v1)  │   compiled into BOTH sides
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
   Tb6600Arduino    Am243Board   TeensyEcatBoard     ◄── one per board
       Board            |             |
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
    tb6600_arduino/     tb6600_arduino_board.*
    sts3215_bus/        sts3215_bus_board.*  (Feetech register protocol)
  comm/                 serial/  ethercat/  udp/  spi/  factory/  (unchanged role)
firmware/                                    ── NEW ──
  common/               joshua_proto_v1.h/.c  (shared with host, same repo)
  arduino_tb6600/       main.cpp, backends, transports, platformio.ini
  teensy_ecat/          …
tools/
  flash/                config-driven flasher + firmware manifest
```

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
board name** — two actuators on the same board share one instance, one comm
handle, one cyclic loop. This replaces (and generalizes) the TU-local serial
`PortResources` cache in `comm_factory.cc`.

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

### 5.5 The two comm hops

```
        HOP 1  "board comm"                    HOP 2  "channel interface"
  PC ────────────────────────► MCU ─────────────────────────────► motor hardware
     serial / UDP / EtherCAT        STEP/DIR pulses / PWM / UART
                                    servo bus / CAN / GPIO
     carried by: Comm transport     executed by: firmware motor backend
     specified in: Board.comm       specified in: Board.channels[i].interface
```

Hop 2 splits between flash time and runtime:

- **Wiring facts** (which MCU pins drive which TB6600, that a servo bus is on
  UART1) are baked into the firmware variant — they cannot change without a
  soldering iron.
- **Tunables** (max pulse rate, direction inversion, bus baud, servo ID per
  channel) live in `Channel.interface_config` and are pushed at `Init()` via
  `CONFIGURE_CHANNEL` protocol commands.
- The IDENTIFY handshake reports per-channel interfaces ("ch0: STEP_DIR,
  ch2: SERVO_BUS_UART"); `Init` fails fast if config and firmware disagree.

`ChannelInterface` also drives motor↔channel validation in one place:

```cpp
// ActionFactory, before constructing the driver:
//   STEPPER_NEMA17 requires STEP_DIR;  STS3215 requires SERVO_BUS_UART;
//   DC_MOTOR requires PWM_DC.
ABSL_RETURN_IF_ERROR(ValidateMotorChannel(actuator.motor_type(), channel_cfg.interface()));
```

### 5.6 Boards without a separate MCU (STS3215 and friends)

"No board" never means bypass the layer — it means **the board is whatever
controller the wire terminates in.** Each STS3215 contains its own MCU
speaking the Feetech register protocol; the servo bus is the degenerate case
where hop 1 and hop 2 are the same wire:

| Concept | General board | `STS3215_BUS` board |
| --- | --- | --- |
| `Board.comm` (hop 1) | serial/UDP/EtherCAT to MCU | the serial bus itself |
| Firmware protocol codec | `joshua_proto_v1` / PDO layout | Feetech register protocol |
| `Channel` (hop 2) | motor slot on the MCU | one servo ID on the bus |
| IDENTIFY handshake | firmware name + proto version | ping servo ID + read model-number register |
| `FirmwareSpec` / flashing | `tools/flash` artifact | omitted — vendor firmware |

One `Sts3215BusBoard` instance per serial port; each daisy-chained servo is a
channel keyed by `servo_id`. The register-protocol encoding moves out of
`Sts3215Driver` into this board; the driver shrinks to motor semantics
(tick↔degree, limits, calibration, idle pose). Bus serialization (one
request/response in flight on the half-duplex UART) gets an owner: the
board's internal bus mutex.

Keeping `board_name` mandatory (rather than optional with an embedded
`comm{}` fallback) keeps `ActionFactory`, validation, and caching on a single
code path. Future smart CAN motors follow the same pattern as a `CAN_BUS`
board.

### 5.7 Runtime behavior: frame-based vs cyclic boards

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
I/O; it stages the value in the board's output image, and the board's single
cyclic loop ships it:

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

This is why boards must be shared instances: N actuators on one AM243 are N
channels writing into one image shipped by one loop on one master socket.

## 6. Config schema

### 6.1 `robot/board/proto/board.proto` (new)

```proto
enum BoardType {
  BOARD_INVALID = 0;
  AM243 = 1;
  TEENSY41_ECAT = 2;
  ARDUINO_TB6600 = 3;
  STS3215_BUS = 4;        // smart-servo bus; the "board" is the servo's own MCU
}

enum ChannelInterface {
  CHANNEL_INTERFACE_INVALID = 0;
  STEP_DIR = 1;           // pulse/direction stepper driver (TB6600 …)
  PWM_DC = 2;
  SERVO_BUS_UART = 3;     // Feetech/Dynamixel-style register bus
  CAN = 4;
  PDO_JOINT = 5;          // slot in a cyclic EtherCAT process-data image
}

message Channel {
  uint32 index = 1;
  ChannelInterface interface = 2;
  oneof interface_config {
    StepDirConfig step_dir = 10;    // max_pulse_rate_hz, invert_dir, enable_active_low
    ServoBusConfig servo_bus = 11;  // servo_id
    PwmConfig pwm = 12;             // frequency_hz, deadband
  }
}

message FirmwareSpec {
  string name = 1;                  // e.g. "tb6600-arduino-eth"
  uint32 min_proto_version = 2;
  // Omitted entirely for vendor-firmware boards (STS3215_BUS).
}

message Board {
  string name = 1;                  // referenced by actuators; cache key
  BoardType board_type = 2;
  robot.comm.Comm comm = 3;         // HOP 1: how the PC reaches the controller
  repeated Channel channels = 4;    // HOP 2: what each slot drives
  FirmwareSpec firmware = 5;
  oneof board_config {
    Am243Config am243_config = 10;  // slave_index, pdo_mapping
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
  SPIKE_MOTOR = 3;
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

### 6.4 Example preset

```proto
robot {
  boards {
    name: "bridge_1"
    board_type: ARDUINO_TB6600
    comm { comm_type: ETHERNET_UDP udp_config { host: "192.168.1.50" port: 5555 } }
    channels { index: 0 interface: STEP_DIR step_dir { max_pulse_rate_hz: 20000 } }
    channels { index: 1 interface: STEP_DIR step_dir { max_pulse_rate_hz: 20000 } }
    firmware { name: "tb6600-arduino-eth" min_proto_version: 1 }
  }
  boards {
    name: "arm_bus"
    board_type: STS3215_BUS
    comm { comm_type: SERIAL serial_config { port: "/dev/ttyUSB0" baudrate: 1000000 } }
    channels { index: 0 interface: SERVO_BUS_UART servo_bus { servo_id: 1 } }
    channels { index: 1 interface: SERVO_BUS_UART servo_bus { servo_id: 2 } }
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
   │                                      "tb6600-arduino-eth, proto v1,
   │                                       ch0: STEP_DIR, ch1: STEP_DIR"
   │ 4. matches firmware{} + channels{}?  ✔
   │        ✘ → "board reports tb6600-arduino-serial-v1 but config expects eth
   │             — run tools/flash --board=bridge_1"
   │ 5. push CONFIGURE_CHANNEL tunables
   ▼
 ValidateMotorChannel(motor_type, channel.interface)
   ▼
 board->OpenChannel(0) → BoardChannel
   ▼
 StepperDriver(channel, actuator_config) → registered as the ROS 2 actuator
```

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

### 7.2 Canonical channel protocol (`joshua_proto_v1`)

One C implementation in `firmware/common/`, compiled into both the host board
classes and the MCU firmware — same repo, so the two sides cannot drift
silently. Frame format:

```
[0xA5 sync][len][proto_ver][cmd][channel][payload...][crc16]

cmd: IDENTIFY           → board_type, fw_name, proto_ver, n_channels,
                          per-channel interface + capability bits
     CONFIGURE_CHANNEL  → push Channel.interface_config tunables
     SET_TARGET         → mode(pos/vel/torque) + float32 value
     GET_FEEDBACK       → position, velocity, fault flags
     ENABLE / DISABLE / ESTOP
```

The frame is transport-agnostic: identical bytes over serial, UDP, or SPI.
EtherCAT is the one exception — cyclic PDO images instead of request/response
frames — so EtherCAT boards use a PDO codec that maps the same logical fields
(target mode/value per channel, feedback per channel) into the image layout.
Where multiple boards run Joshua firmware over EtherCAT (AM243, Teensy+
EasyCAT), they should share one canonical PDO layout so the host codec is
written once.

### 7.3 How the codec and protos are shared across boards

Two different artifacts are "shared", in two different ways:

```
 ① protobuf (.proto files)   → shared across HOST components only (config language)
 ② wire codec (plain C)      → shared between HOST and every FIRMWARE (byte language)
```

**Protobuf never crosses the wire to the MCU.** Protobuf is the config
language — right on Linux, wrong for an ATmega328 (code size, heap, varint
parsing inside a control loop). The host board class is the translator: it
reads `Channel.interface_config` (protobuf) and emits `CONFIGURE_CHANNEL`
frames (C codec). Firmware never links protobuf; the host never touches raw
frame bytes outside the codec.

```
                .pbtxt config (protobuf ①)
                       │  host-only world
                       ▼
        Tb6600ArduinoBoard / TeensyEcatBoard / …
                       │  proto fields → fixed C frames
                       ▼
              joshua_proto_v1  (plain C ②)          ◄── THE shared artifact
                       │  raw bytes on the wire
                       ▼
              MCU firmware (compiled with the same joshua_proto_v1)
```

**② is shared by compiling one source file into every binary that touches
the wire** — single source, multiple builds:

```
 firmware/common/joshua_proto_v1.{h,c}      ◄── one source of truth
        │
        ├── Bazel cc_library //firmware/common:joshua_proto_v1
        │         ├─► linked into Tb6600ArduinoBoard   (host, x86)
        │         └─► linked into TeensyEcatBoard      (host, x86)
        │
        ├── PlatformIO lib in firmware/arduino_tb6600/  (AVR build)
        └── PlatformIO lib in firmware/teensy_ecat/     (ARM build)
```

For that to work the codec must be lowest-common-denominator C: no malloc,
no libc beyond `stdint`, explicit little-endian byte packing, pure
encode/decode functions over caller-provided buffers:

```c
// firmware/common/joshua_proto_v1.h — sketch
int jp1_encode_set_target(uint8_t* buf, size_t cap, uint8_t channel,
                          jp1_mode_t mode, float value);   // → frame length or -1
int jp1_decode(const uint8_t* buf, size_t len, jp1_frame_t* out);  // sync+crc+version
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
  uint8_t  mode;       // jp1_mode_t
  int32_t  target;     // native units
  uint16_t sequence;
} jp1_pdo_out_channel_t;   // host writes, firmware reads

typedef struct __attribute__((packed)) {
  int32_t  position;
  int16_t  velocity;
  uint16_t fault_flags;
} jp1_pdo_in_channel_t;    // firmware writes, host reads
```

Every board that adopts this canonical layout (AM243 actuator firmware,
future Teensy/EasyCAT) is covered by **one** host-side PDO codec — the
motivation behind open question §12.4. Vendor-controlled protocols are the
exception and get their own host-side codec each: the TI demo byte-walk
(`am243_pdo_codec`, kept because we do not control the TI demo firmware) and
the Feetech register protocol (`Sts3215BusBoard`).

**① is ordinary host-side reuse:** `board.proto` is imported by
`robot.proto`, compiled once by Bazel, and consumed by every host component
(`BoardFactory`, `ActionFactory`, validation tests, future Python parity).
Firmware's only contact with proto-derived data is indirect — the values the
host copies out of `Channel.interface_config` into `CONFIGURE_CHANNEL`
frames.

| Artifact | Language | Shared by | Crosses the wire? |
| --- | --- | --- | --- |
| `board.proto`, `action.proto` | protobuf | host components only | no — config only |
| `joshua_proto_v1.c` | plain C | host boards + all frame-based firmware | **defines** the wire bytes |
| `joshua_pdo_v1.h` | packed C structs | host + all Joshua EtherCAT firmware | **defines** the PDO image |
| Feetech / TI-demo codecs | C++ host-side only | one vendor board each | speaks the vendor's format |

Rejected alternative: nanopb could put protobuf on the MCU wire, unifying ①
and ② — not worth it (varint decode in the control loop, nondeterministic
frame sizes, and EtherCAT needs fixed layouts anyway). Fixed C frames are
simpler and testable to the byte.

### 7.4 Firmware source layout and build variants

```
firmware/
  common/
    joshua_proto_v1.h/.c        # shared with host
  arduino_tb6600/
    main.cpp                    # loop { transport_poll(); dispatch(); step_service(); }
    backend_stepdir.cpp         # STEP/DIR/ENA pulse gen, accel ramps
    transport_serial.cpp
    transport_w5500.cpp         # Ethernet shield (UDP)
    transport_spi_slave.cpp
    platformio.ini
```

```ini
; one env per wiring variant → one artifact each
[env:tb6600-serial]     build_flags = -DJOSHUA_TRANSPORT_SERIAL
[env:tb6600-eth-w5500]  build_flags = -DJOSHUA_TRANSPORT_W5500
[env:tb6600-spi]        build_flags = -DJOSHUA_TRANSPORT_SPI_SLAVE
```

Transport is a **compile-time variant**, producing unambiguous artifacts
(`tb6600-arduino-eth-v1.hex`). Prefer variants over a fat binary with runtime
transport selection: small MCUs lack the flash/RAM for unused stacks, and the
artifact name states exactly what is on the board. Keep the variant model
even on large MCUs (AM243) for uniformity.

### 7.5 Flash tooling

`tools/flash/` reads the same robot config, resolves each board's needed
artifact from a firmware manifest, and invokes the right flasher:

```
bazel run //tools/flash -- --config=so100/teleop.pbtxt --board=bridge_1
  → board_type=ARDUINO_TB6600, comm=ETHERNET_UDP → tb6600-arduino-eth-v1
  → avrdude -p m328p -c arduino -U flash:w:tb6600-arduino-eth-v1.hex
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
        └► joshua_proto_v1 encode → comm_->Send → wire → MCU decode
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
(TB6600+Arduino → AM243 for the same stepper): edit `boards{}` + `board_name`,
zero motor-driver changes.

## 9. Risks & mitigations

| Risk | Mitigation |
| --- | --- |
| Proto migration breaks existing presets | Phase 1 keeps deprecated fields working; presets migrate in Phase 4; validation test (`config_preset_validation_test`) covers both during transition |
| Cyclic-loop threading bugs (staged image vs loop) | Single writer (loop) for wire I/O; channel staging behind a mutex or lock-free slots; reuse existing `ethercat_status` WKC validation |
| Codec drift between host and firmware | One shared C source in-repo; proto_ver in every frame; handshake rejects version mismatch |
| ACK round-trip too slow on chatty buses | Frame protocol allows fire-and-forget mode per command class later; measure first |
| STS3215 refactor regresses working so100 robots | Port behind the new layer with byte-identical bus traffic; keep `test_sts3215_encoder.py` and teleop presets as regression gates |
| AM243 real firmware PDO layout unknown | Demo codec stays isolated behind `Am243PdoMapping` exactly as today; board layer does not depend on the final layout |
| Small-MCU RAM/flash limits (ATmega328 + W5500) | Codec is dependency-free C; per-variant builds strip unused transports; Teensy/ESP32 as fallback targets |

## 10. Phased rollout (TODO)

Each phase lands green and independently revertible.

### Phase 1 — Proto groundwork
- [ ] Add `robot/board/proto/board.proto` (`Board`, `Channel`,
      `ChannelInterface`, `FirmwareSpec`, `BoardType`).
- [ ] Add `repeated robot.board.Board boards` to `config/proto/robot.proto`.
- [ ] Add `MotorType`, `board_name`, `channel` to `Actuator`; mark
      `ActuatorType` board-flavored values and embedded `comm` deprecated
      (still functional).
- [ ] Extend `comm.proto` with `ETHERNET_UDP` (+ `UdpConfig`); SPI deferred
      until an SBC host needs it.
- [ ] Update `config_preset_validation_test` for both old and new shapes.

### Phase 2 — Board layer skeleton
- [ ] `robot/board/interfaces/`: `BoardInterface`, `BoardChannel`,
      `TargetMode`, `ChannelFeedback`.
- [ ] `robot/board/factory/`: `BoardFactory` with instance cache keyed by
      board name.
- [ ] `ValidateMotorChannel(motor_type, channel_interface)` compatibility
      table + tests.
- [ ] Move the serial `PortResources` cache out of `comm_factory.cc` behind
      the board factory.

### Phase 3 — Port AM243
- [ ] `robot/board/am243/Am243Board`: absorbs transport creation, split
      LRD/LWR validation (currently in `ActionFactory`), PDO region mapping,
      cyclic exchange loop, WKC checks.
- [ ] Move `am243_pdo_codec.*` to `robot/board/am243/`; keep
      `AM243_PDO_MAPPING_TI_DEMO` isolation.
- [ ] Replace `Am243EthercatDriver` with a thin motor driver over
      `BoardChannel` (or fold into a generic joint driver).
- [ ] Rewire `am243_demo_smoke`, `am243_driver_smoke`, `am243_config_smoke`
      to the new path — these are the hardware regression gates.
- [ ] Update `docs/am243_ethercat.md` boundaries section.

### Phase 4 — Port STS3215
- [ ] `robot/board/sts3215_bus/Sts3215BusBoard`: Feetech register protocol,
      per-port instance, bus mutex, servo-ping IDENTIFY.
- [ ] Slim `Sts3215Driver` to motor semantics over `BoardChannel`
      (byte-identical bus traffic as acceptance bar).
- [ ] Migrate so100 presets to `boards{}` + `board_name`; keep teleop working.
- [ ] Remove deprecated `ActuatorType` values, embedded `Actuator.comm`, and
      the old factory arms.

### Phase 5 — Prove the matrix (first real second board)
- [ ] Define `joshua_proto_v1` frame codec in `firmware/common/` (C, shared;
      golden-bytes unit tests on host CI, §7.3).
- [ ] `firmware/arduino_tb6600/` with serial transport variant;
      `backend_stepdir`.
- [ ] Host side: `Tb6600ArduinoBoard` + generic `StepperDriver` +
      `FrameTransport` seam on `Serial`.
- [ ] End-to-end smoke: pbtxt → ActionFactory → BoardFactory → frames →
      Arduino → TB6600 → stepper moves.
- [ ] Add the UDP (W5500) firmware variant + `UdpTransport` to demonstrate a
      transport swap with zero host-code changes.

### Phase 6 — Firmware tooling & handshake hardening
- [ ] `IDENTIFY` capability bits + per-channel interface report;
      `CONFIGURE_CHANNEL` tunables push.
- [ ] `FirmwareSpec` verification in every `Board::Init` with actionable
      error text.
- [ ] `tools/flash/` + firmware manifest
      (`(board_type, transport) → artifact + flash method`).
- [ ] Docs: firmware contribution guide (how to add a board / a transport
      variant / a backend).

## 11. Acceptance criteria

- A stepper motor moves via TB6600+Arduino over serial **and** over UDP by
  changing only `Board.comm` and reflashing the matching variant — zero host
  code changes.
- Existing so100 teleoperation and AM243 smoke tests pass through the new
  layers with unchanged wire behavior.
- Two actuators on one board share one board instance, one comm handle, one
  cyclic loop (verified by test).
- A wrong-firmware or wrong-transport board is rejected at `Init()` with an
  error that names the board and the `tools/flash` command to fix it.
- Adding a hypothetical new board requires: one board class, one firmware
  target, config — and **zero** changes to motor drivers or comm transports
  (demonstrated in review by the Phase 5 diff shape).

## 12. Open questions

1. **Units at the seam** — native units (steps/ticks) with conversion in the
   motor driver (current proposal), or SI at the seam with per-channel scale
   pushed via `CONFIGURE_CHANNEL`? Decide before Phase 2 freezes
   `BoardChannel`.
2. **Feedback pull vs push** — `ReadFeedback()` polling is fine for
   frame-based boards; should cyclic boards also expose a subscription/latest
   cache for encoder publishers, and does the perception layer read through
   the same board instance?
3. **Python parity** — `action_factory.py` / `comm_factory.py` mirror the C++
   factories today; does the board layer need a Python implementation for the
   Pybricks/mock paths, or do those stay driver-direct until needed?
4. **Canonical EtherCAT PDO layout** — one shared Joshua PDO schema for AM243
   and future Teensy/EasyCAT firmware: fixed 8-byte-per-channel slots, or
   ESI/SDO-described dynamic mapping?
5. **Cyclic loop ownership** — one thread per EtherCAT board vs one shared
   host real-time loop for all cyclic boards; latency and jitter targets TBD.
6. **ESTOP semantics** — protocol-level broadcast (all channels, all boards)
   and its guarantees on frame-based vs cyclic transports.
