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

One file per axis value, never one per combination. Two interfaces hold the
axes apart: `BoardInterface` (init, channels, teardown) and `BoardChannel`
(`Enable`/`SetTarget`/`ReadFeedback`), which is all a motor driver ever sees.
**Nothing in this document changes either one** — the rework in §4 happens below
them, in two separate *transport* interfaces, which is why it touches no motor
driver.

## 2. Status

**Landed:** the protos, both board interfaces, `BoardFactory` and
`ValidateMotorChannel`;
AM243 over EtherCAT *and* serial; the Feetech/STS3215 bus board and so100
actuator presets; the `joshua_wire_v1` C codec shared host↔firmware with
golden-byte tests; Teensy 4.1 and ESP32, hardware-verified; the Python robot
layer deleted.

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
| **F3** | `SendAndReceive()` takes **already-framed** bytes, so every transport inherits serial's `0xA5`/crc16 — redundant on UDP, fatal on CAN (39-byte frame, 8-byte MTU). | `frame_transport.h:39` |
| **F4** | Fixed response length is baked in at three levels at once; variable-length payloads break all three. | `frame_transport.h` |
| **F5** | Three enums hand-mirrored with no `static_assert`. One already drifted: `JW1_BOARD_ESP32 = 8` vs `ESP32 = 7`. | `joshua_wire_v1.h:93` |
| **F6** | `Esp32Board`'s only real override is a one-time 2 s sleep at `Init`: opening the port asserts DTR, which the CP2102 bridge wires to reset, so the board reboots and the first exchange waits it out — nothing per message. A property of the **link**, not of ESP32 silicon. | `esp32_board.cc` |

## 4. Target: two planes

Proto packets should reach the MCU over any comm — but not on the control
loop, where EtherCAT PDO needs a fixed-size image, CAN's MTU is 8 bytes, and
varint decode is data-dependent. Split the wire as every fieldbus already does
(EtherCAT CoE/PDO, CANopen SDO/PDO). A **plane** is one of the two paths an
operation can take to the board:

| Plane | Operations | Payload | Rate |
| --- | --- | --- | --- |
| **Message** | `IDENTIFY`, `CONFIGURE_CHANNEL`, `ENABLE`/`DISABLE`/`ESTOP` | proto | at `Init`, occasional |
| **Cyclic** | `SET_TARGET`, `GET_FEEDBACK` | fixed struct | loop rate |

An 8-byte cyclic struct fits one CAN frame with no fragmentation; that should
size it. (Serial answers `SET_TARGET` request/response today — question 2.)
The critical change: `Exchange` takes a **payload**, not a frame, so
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

One engine per plane — `MessageBoard(identity, transport)` and
`CyclicBoard(identity, transport, layout)` — replaces every per-board class.
**Identity** is the data saying which board this is: `board_type`, channel
count, expected firmware version, the `jw1_*` enum values. Passing it to the
constructor rather than encoding it in the type is what removes `TeensyBoard`
and `Esp32Board`. `BoardFactory` then resolves three axes independently instead
of switching on one, so adding EtherCAT to a Teensy is a `.pbtxt` edit.

## 5. Plan

Architecture first: define every interface, both planes and the dependency rules
before writing any transport. Steps 1–2 add no capability — they establish the
structure and move what exists onto it. Only then is a new comm or a new wire
format a one-file change, which is the entire claim of this RFC.

**Step 1 — Establish the layers.** *Headers, fakes and build rules; no behaviour change.*
- [ ] Declare both transport interfaces in `robot/comm/interfaces/`: `MessageTransport` (payload in, payload out) and `CyclicTransport` (image swap). Neither names a comm type.
- [ ] Declare `BoardIdentity` and the two engines, `MessageBoard` and `CyclicBoard`, in `robot/board/engines/`. Identity is constructor data; no per-board subclass exists anywhere (F1, F2).
- [ ] Settle which plane owns `SET_TARGET` (question 2). This sizes the engines and cannot be deferred past this step.
- [ ] Enforce the dependency direction in Bazel visibility: motor drivers see only `BoardChannel`; the engines see only `MessageTransport` and `CyclicTransport`, never a concrete one; concrete transports are visible to the comm factory alone. A board target that can name a concrete transport is a layering bug.
- [ ] Fakes for both transport interfaces, plus engine tests that run with no hardware.

**Step 2 — Move the existing boards onto the architecture.** *No wire change; behaviour identical.*
- [ ] `FrameTransport` → a serial `MessageTransport`; framing moves inside it and serial keeps today's exact bytes, so the golden-byte tests do not change (F3, F4).
- [ ] `JoshuaWireBoard` → `MessageBoard`; delete `TeensyBoard` and `Esp32Board`; settle delay → serial transport config (F1, F2, F6).
- [ ] Generate `BoardIdentity` and the `jw1_*` enums from the protos; fix `JW1_BOARD_ESP32` to `7` (F5).
- [ ] AM243's EtherCAT path → `CyclicBoard` + an EtherCAT `CyclicTransport`, TI PDO map as an `ImageLayout`; `serial_mode_` and the hand-forwarding disappear (F2).
- [ ] `BoardFactory` resolves three axes independently: identity, plane, transport.
- [ ] Acceptance: so100 teleop and the AM243 EtherCAT path behave identically, and no per-board class remains.

**Step 3 — Prove the axes are independent.** *No wire change; the payoff.*
- [ ] A UDP `MessageTransport` plus the matching firmware variant: same board, same firmware logic, new link — **no new class and no engine change**.

**Step 4 — Proto payloads on the message plane.** *Wire change; firmware flash.*
- [ ] nanopb on firmware, length-prefixed responses, retire `JW1_*_RESPONSE_PAYLOAD_LEN` (F4). Gated on question 1.

**Step 5 — New axis values on the finished architecture.**
- [ ] CAN, with ISO-TP fragmentation inside the transport; EtherCAT mailbox (CoE) on the message plane. Each should be one new file and a `.pbtxt` edit — if it is not, steps 1–2 were wrong.

**Independent of the above**
- [ ] **Perception** — route `Sts3215Encoder` through `FeetechBusBoard`: it writes raw Feetech frames to the shared port from publisher timers, bypassing the bus mutex. Then `SensorType`, optional `board_name`/`channel`, `ValidateSensorChannel`.
- [ ] **Firmware tooling** — `tools/flash` + manifest keyed by `(board_type, comm)`; `FirmwareSpec` verification in `Init`; IDENTIFY capability bits.
- [ ] **Proto cleanup** — remove deprecated `ActuatorType` values and `Actuator.comm`.
- [ ] **ROS 2 vendor boards** — `ROS2` comm type, `ROS2_VENDOR_ROBOT` with graph validation, `MOBILE_BASE_VELOCITY`, proven on TurtleBot 4.

## 6. Open questions

1. **Can every firmware image afford nanopb?** ESP32 has headroom (20.7 % flash); Arduino Uno may need a compact non-proto encoding. *Blocks step 4.*
2. **Is `SET_TARGET` always cyclic?** Request/response on serial today. If a board needs both planes at once, the engines must compose rather than exclude — a shape decision, so it *blocks step 1.*
3. **CANopen, or raw CAN with ISO-TP?** CANopen's SDO/PDO split partly duplicates our planes.
4. **Keep crc16 on links that already checksum?** Redundant over UDP and CAN.
5. **Is BLE on the roadmap?** Removed with the Python layer; if speculative, only avoid precluding it.
6. **Cyclic feedback** — poll `ReadFeedback()`, or a latest-value cache for perception publishers?

## 7. Done when

- The same board type runs over two comms by editing `Board.comm` — no new class.
- Adding a board is a `.pbtxt` edit plus a firmware target; drivers, transports and the factory shape are untouched.
- No board target can name a concrete transport, and no enum is mirrored by hand between host and firmware.
- so100 teleop and the AM243 EtherCAT path pass unchanged at every step.

## Appendix: v1 section numbers

Code comments across `robot/board/`, `robot/comm/` and the protos cite the
old numbering. They resolve against the archived copy
(`git show 2dca167:docs/BOARD_LAYER_RFC.md`); retarget them opportunistically
as those files are touched.

| Cited as | Subject | Now |
| --- | --- | --- |
| §5.3 | the two board interfaces, unit convention, instance caching | §1 (still true) |
| §5.5 | comm vs drive legs, motor↔drive validation | §1 (still true) |
| §5.6 | boards without a separate MCU (Feetech, HOST_GPIO) | archived — behaviour unchanged |
| §7.2 / §7.3 | `joshua_wire_v1` framing, shared-codec rules | archived; superseded by §4 |
| §7.5 | the firmware channel table | archived — behaviour unchanged |
| §12.7 | torque semantics on `BoardChannel` | resolved in phase 4; see `board_channel.h` |
| §10 Phase N | the old rollout checklist | §5 |
