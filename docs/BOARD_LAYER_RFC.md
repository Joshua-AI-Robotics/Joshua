# Board Layer RFC

Status: **phases 1–5 landed.** This revision keeps only what is still open.
Companion to: [ARCHITECTURE.md](ARCHITECTURE.md), [am243_ethercat.md](am243_ethercat.md)

The original 1,885-line RFC — full rationale for everything already built — is
preserved in git: `git show 2dca167:docs/BOARD_LAYER_RFC.md`.

## 1. The model (settled)

| Axis | Question | Config field |
| --- | --- | --- |
| **Motor** | what physically moves | `Actuator.motor_type` |
| **Board** | what runs the low-level control loop | `Board.board_type` |
| **Comm** | how the host reaches the board | `Board.comm.comm_type` |
| **Drive** | how the board moves the motor | `Channel.drive` |

One file per axis value, never one per combination. Two seams carry it and
both stay unchanged: `BoardInterface` (init, channels, teardown) and
`BoardChannel` (`Enable`/`SetTarget`/`ReadFeedback`), which is all a motor
driver ever sees.

## 2. Status

**Landed:** the protos and both seams; `BoardFactory` with instance cache and
`ValidateMotorChannel`; AM243 over EtherCAT *and* serial; the Feetech/STS3215
bus board and so100 actuator presets; the `joshua_wire_v1` C codec shared
host↔firmware with golden-byte tests; Teensy 4.1 and ESP32, hardware-verified;
the Python robot layer deleted.

**Open:**

| Item | State |
| --- | --- |
| Comm axis beyond serial for `joshua_wire` boards | blocked — §3 |
| UDP transport (`ETHERNET_UDP` is in the proto, unimplemented) | open |
| Perception through the board layer | open |
| Flash tooling, `FirmwareSpec` check, IDENTIFY capability bits | open |
| Deprecated `ActuatorType` values and `Actuator.comm` | still present |
| ROS 2 vendor-robot boards | open |

## 3. Problem: the axes re-conflated one level down

§1 exists to stop `board × transport` being baked into a type; the landed
layer does it again in the class hierarchy. `TeensyBoard : public
JoshuaWireBoard` asserts at compile time that a Teensy speaks joshua_wire over
serial — so the Teensy+EasyCAT combination that `board.proto` calls out by
name needs a new class today.

| | Finding | Where |
| --- | --- | --- |
| **F1** | `BoardFactory` switches on `board_type` alone, never `comm_type` — identity is bound to protocol at compile time. | `board_factory.cc:29` |
| **F2** | `Init`/`OpenChannel`/`Teardown` are `final`, so dual-transport boards cannot subclass; `Am243Board` needs composition plus a `serial_mode_` bool. | `joshua_wire_board.h:55` |
| **F3** | `SendAndReceive()` takes **already-framed** bytes, so every transport inherits serial's `0xA5`/crc16. Redundant on UDP; fatal on CAN — 39-byte frame, 8-byte MTU. | `frame_transport.h:39` |
| **F4** | Fixed response length is baked in at three levels at once; variable-length payloads break all three. | `frame_transport.h` |
| **F5** | Three enums hand-mirrored with no `static_assert`. One already drifted: `JW1_BOARD_ESP32 = 8` vs `ESP32 = 7`. | `joshua_wire_v1.h:93` |
| **F6** | `Esp32Board`'s only real override is a 2 s settle delay caused by the CP2102 bridge — a property of the USB-serial **link**, not of ESP32 silicon. | `esp32_board.cc` |

## 4. Target: two planes

Proto packets should reach the MCU over any comm — but not on the control
loop, where EtherCAT PDO needs a fixed-size image, CAN's MTU is 8 bytes, and
varint decode is data-dependent. Split the wire as every fieldbus already does
(EtherCAT CoE/PDO, CANopen SDO/PDO):

| Plane | Operations | Payload | Rate |
| --- | --- | --- | --- |
| **Message** | `IDENTIFY`, `CONFIGURE_CHANNEL`, `ENABLE`/`DISABLE`/`ESTOP` | proto | at `Init`, occasional |
| **Cyclic** | `SET_TARGET`, `GET_FEEDBACK` | fixed struct | loop rate |

An 8-byte cyclic struct fits one CAN frame with no fragmentation; that should
size it. The critical change: `Exchange` takes a **payload**, not a frame, so
framing and fragmentation live inside each transport.

```cpp
class MessageTransport {  // acyclic: one request, one response
  virtual absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t>) = 0;
};

class CyclicTransport {  // fixed image swapped every cycle; no per-request response
  virtual absl::Status WriteOutputs(absl::Span<const uint8_t>) = 0;
  virtual absl::Status Exchange() = 0;
  virtual absl::StatusOr<absl::Span<const uint8_t>> ReadInputs() = 0;
};
```

Identity becomes constructor data (`BoardIdentity`, generated from
`BoardType`), and one engine per plane — `MessageBoard(identity, transport)`,
`CyclicBoard(identity, transport, layout)` — replaces the per-board classes.
`BoardFactory` does three independent lookups instead of one switch, so adding
EtherCAT to a Teensy is a `.pbtxt` edit.

| Today | Becomes | Why |
| --- | --- | --- |
| `BoardInterface`, `BoardChannel` | unchanged | already protocol-neutral |
| `FrameTransport` | `MessageTransport` | transport owns its framing (F3) |
| `JoshuaWireBoard` | `MessageBoard` | identity by constructor (F1, F2) |
| `TeensyBoard`, `Esp32Board` | **deleted** | two constants each; delay → transport config (F6) |
| `Am243Board` | split across planes | `serial_mode_` disappears (F2) |
| `jw1_*` enums | generated from proto | drift becomes impossible (F5) |

## 5. Plan

Each step ships independently. Only step 4 touches the wire.

**1 — Identity as data.** *No wire change; unblocks Teensy + EasyCAT.*
- [ ] Generate `BoardIdentity` and the `jw1_*` enums from the protos; fix `JW1_BOARD_ESP32` to `7` (F5).
- [ ] Delete `TeensyBoard`/`Esp32Board`; settle delay → serial transport config (F1, F6).
- [ ] `BoardFactory` does three lookups (F1).

**2 — Framing into the transport.** *No wire change; pure refactor.*
- [ ] `FrameTransport` → `MessageTransport` (F3, F4); serial keeps today's exact bytes.
- [ ] Move the serial `PortResources` cache behind the board factory.

**3 — A second comm.** *The proof.*
- [ ] `UdpMessageTransport` + firmware variant: same board, same firmware logic, **no new class**.

**4 — Proto payloads on the message plane.** *Wire change; firmware flash.*
- [ ] nanopb on firmware, length-prefixed responses, retire `JW1_*_RESPONSE_PAYLOAD_LEN` (F4). Gated on questions 1–2.

**5 — Extract the cyclic plane.** *Refactor; AM243 hardware retest.*
- [ ] `CyclicTransport` + `CyclicBoard`; AM243 EtherCAT moves onto them, TI PDO map as an `ImageLayout`. CAN lands here.

**Independent of the above**
- [ ] **Perception** — route `Sts3215Encoder` through `FeetechBusBoard`: it writes raw Feetech frames to the shared port from publisher timers, bypassing the bus mutex. Then `SensorType`, optional `board_name`/`channel`, `ValidateSensorChannel`.
- [ ] **Firmware tooling** — `tools/flash` + manifest keyed by `(board_type, comm)`; `FirmwareSpec` verification in `Init`; IDENTIFY capability bits.
- [ ] **Proto cleanup** — remove deprecated `ActuatorType` values and `Actuator.comm`.
- [ ] **ROS 2 vendor boards** — `ROS2` comm type, `ROS2_VENDOR_ROBOT` with graph validation, `MOBILE_BASE_VELOCITY`, proven on TurtleBot 4.

## 6. Open questions

1. **Can every firmware image afford nanopb?** ESP32 has headroom (20.7 % flash); Arduino Uno may need a compact non-proto encoding. *Blocks step 4.*
2. **Is `SET_TARGET` always cyclic?** Request/response on serial today. If boards straddle planes, hybrid composition is needed at step 1, not step 5. *Blocks step 4.*
3. **CANopen, or raw CAN with ISO-TP?** CANopen's SDO/PDO split partly duplicates our planes.
4. **Keep crc16 on links that already checksum?** Redundant over UDP and CAN.
5. **Is BLE on the roadmap?** Removed with the Python layer; if speculative, only avoid precluding it.
6. **Cyclic feedback** — poll `ReadFeedback()`, or a latest-value cache for perception publishers?

## 7. Done when

- The same board type runs over two comms by editing `Board.comm` — no new class.
- Adding a board is a `.pbtxt` edit plus a firmware target; drivers, transports and the factory shape are untouched.
- No enum is mirrored by hand between host and firmware.
- so100 teleop and the AM243 EtherCAT path pass unchanged at every step.

## Appendix: v1 section numbers

Code comments across `robot/board/`, `robot/comm/` and the protos cite the
old numbering. They resolve against the archived copy
(`git show 2dca167:docs/BOARD_LAYER_RFC.md`); retarget them opportunistically
as those files are touched.

| Cited as | Subject | Now |
| --- | --- | --- |
| §5.3 | the two seams, unit convention, instance caching | §1 (still true) |
| §5.5 | comm vs drive legs, motor↔drive validation | §1 (still true) |
| §5.6 | boards without a separate MCU (Feetech, HOST_GPIO) | archived — behaviour unchanged |
| §7.2 / §7.3 | `joshua_wire_v1` framing, shared-codec rules | archived; superseded by §4 |
| §7.5 | the firmware channel table | archived — behaviour unchanged |
| §12.7 | torque semantics at the seam | resolved in phase 4; see `board_channel.h` |
| §10 Phase N | the old rollout checklist | §5 |
