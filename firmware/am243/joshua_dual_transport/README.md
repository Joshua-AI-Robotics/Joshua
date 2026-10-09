# LP-AM243 — JoshuaWire 0.0.2 serial and EtherCAT firmware

One AM243 image that keeps TI's EtherCAT simple demo active while also serving
JoshuaWire (JW) `0.0.2` on UART0 at 115200 baud. The build layers Joshua-owned source
and a small patch over the externally installed TI Industrial Communications
SDK; it does not modify or vendor the SDK.

The default image serves JW UART alongside TI's EtherCAT echo demo. Its
serial channel reports `STEP_DIR` and implements command/response handling
in software; it does not drive STEP/DIR GPIOs or move motors.
Its dedicated task composes the shared JW endpoint with the nonblocking
[`transport_uart`](src/transport_uart.h) adapter. The adapter exclusively polls
UART0's 64-byte FIFO, uses the common incremental serial assembler, and accepts
a complete response only when the TX FIFO is empty. The task retains blocked
responses without repeating a command and yields between iterations. Binary
UART ownership starts after SDK startup logs are disabled; no SDK UART
transactions or other writers may share that FIFO.

## Prerequisites

- TI Industrial Communications SDK `09.00.00.03`
- CCS/ARM Clang, PRU CGT, and SysConfig versions listed in the adjacent
  `ti_ethercat_simple_demo_v1/README.md`
- The SDK configured by the existing AM243 setup scripts

## Install

Follow `../ti_ethercat_simple_demo_v1/README.md`. No additional packages are
required beyond `patch`, `make`, and the existing TI toolchain.

## Build

```bash
firmware/am243/joshua_dual_transport/scripts/build.sh
```

Artifacts are written under the ignored `out/` directory. The build uses a
temporary working tree and leaves TI SDK sources unchanged.

The default UART outputs are `am243_dual_transport_jw.release.*`. The script
forwards arguments to `make`, allowing compiler/tool path overrides. EtherCAT
in this image remains TI's echo demo; use the separate JW profile below for
Joshua EtherCAT runtime. See [JW limits](../../README.md#joshuawire-serial)
and the [serial validation procedure](../../../docs/JOSHUA_WIRE_VALIDATION.md).

### Opt-in JW EtherCAT profile

The same overlay also builds a separate **EtherCAT-only** JoshuaWire artifact:

```bash
JOSHUA_ETHERCAT_PROFILE=jw \
JOSHUA_COMM_WATCHDOG_US=500000 JOSHUA_TARGET_WATCHDOG_US=250000 \
firmware/am243/joshua_dual_transport/scripts/build.sh
```

Those intervals are software-demo examples, not validated motor-safety limits.
Both must be explicit (10000–999999999 microseconds); there are no watchdog
defaults. Outputs are `out/am243_ethercat_jw.release.*`. The default profile
remains `ti-demo`, building JW UART alongside TI's echo PDOs.
The JW profile starts no UART protocol task; the console remains available for
SDK logs. It has one software-only channel and **no STEP/DIR GPIO backend**.

Source responsibilities:

- `src/joshua_commands.{h,c}`: transport-neutral software channel operations,
  shared with the UART artifact (formerly `joshua_serial_commands`).
- [`../../common/joshua_ethercat_profile.h`](../../common/joshua_ethercat_profile.h)
  and `.c`: shared board/stack-independent object/PDO protocol, one session
  across CoE and PDO, retained replies, and per-channel freshness watchdogs.
- `src/joshua_ethercat_ti.{h,c}`: SDK object registration, complete image copies,
  callback serialization and an independent RTOS watchdog task. Supplies AM243
  identity and software-channel callbacks to the shared profile; a different
  board supplies its own stack/drive adapter, not a new host transport.
- `patches/jwec_profile.patch`: replaces TI's demo object/PDO setup and dispatch
  in a temporary SDK source copy. TI demo EEPROM-persistence callbacks are not
  installed for this profile; the SDK builds the profile from its new mapping.

The descriptor reports `am243-ec-jw`, JW/layout-v1, 80-byte input/output images
and transport bits `6` (CoE + PDO). The matching fixed mapping is:

| Direction | PDO | Mapped entries | Size |
| --- | --- | --- | --- |
| Master → slave | `0x1600` | `0x7000:01` through `:14` (hex), 20 × UINT32 | 80 bytes |
| Slave → master | `0x1a00` | `0x6000:01` through `:14` (hex), 20 × UINT32 | 80 bytes |

Assignments `0x1c12`/`0x1c13` each contain the corresponding single fixed PDO.
The image arrays are PDO-only; CoE accesses cannot modify their contents.
All management objects use subindex zero, without Complete Access:
`0x2000` descriptor (36 bytes, read), `0x2001` session/reset (8, read/write),
`0x2010` request (76, write), `0x2011` retained response (76, read), and
`0x2012` exact-generation acknowledgment (4, write). Byte layouts are defined
in [the shared contract](../../common/joshua_wire_ethercat.h).

The evaluation identity inherits TI's SDK vendor setting; real SII discovery
reported `0xe000059d`, with Joshua profile product `0x4a570002` and revision
`0x00020001`. This is not a registered
commercial product identity. Resolve identity/redistribution requirements
before public hardware distribution. No standalone ESI XML is provided yet;
the mapping above documents the profile, and a cached TI-demo ESI is incompatible.

Only a **new valid command**, not repeated reads of an unchanged PDO output
image, refreshes the command-progress watchdog. ENABLE starts the target grace
period; successful SET_TARGET refreshes target freshness. GET_FEEDBACK can feed
command progress but cannot keep an old target alive. An enabled host must issue
fresh targets within both configured limits. This is deliberately stricter than
a packet-arrival watchdog: the SDK application loop alone does not establish
that a new EtherCAT output packet arrived.

The independent task checks time every 1 ms plus RTOS scheduling delay. Expiry,
loss of OP or protocol faults disable the software channel, clear its target
and latch ESTOP. Feedback fault bits are `0x1` command-progress timeout, `0x2`
stale target, `0x4` state/clock fault and `0x8` malformed PDO. Only reset to a
different nonzero session clears latches; repeating the current reset is a no-op.
An all-zero output is an explicit stop; generation zero with the active session
only cancels the command slot. Replies remain until the matching acknowledgment;
duplicates never execute again or refresh watchdogs.

Native tests exercise the production core with a controlled clock and through
the host's paired CoE/PDO adapters. Production Ethernet timing requires hardware
qualification. Software watchdog behavior does not establish physical motor
safety, CPU-halt coverage or hard-real-time timing.

### TODO: Retire TI-stack profiles after replacement qualification

Keep the TI-stack JW EtherCAT profile until the separately reviewed replacement
passes AM243 hardware qualification and more than 60 minutes of continuous
fresh target/feedback traffic. Changing slave stacks does not resolve the known
host NIC timing limitation. The default UART image still includes the TI demo;
convert it to UART-only when retiring the evaluation stack.

## Host integration

The [JW EtherCAT config](../../../config/README.md#joshuawire-over-ethercat)
now selects paired adapters through CommFactory and the shared board engine.
Native integration tests run that entire path against this production firmware
core. Native coverage does not establish production Ethernet timing or
physical-output safety.

## Flash

Not automated by this target. After the image is built and reviewed, adapt the
existing TI demo flash configuration to point at:

```text
out/am243_dual_transport_jw.release.appimage.hs_fs
```

Use the same SDK 09 bootloader/application offsets. Verify both bootloader and
application after flashing; do not use the vendor TI-demo application path for
a JoshuaWire image.

The separate JW EtherCAT profile uses
`out/am243_ethercat_jw.release.appimage.hs_fs`; it is not the UART image.

Flashing remains a deliberate hardware operation and must not happen as part
of build or test.

## Verify

Follow the [serial validation guide](../../../docs/JOSHUA_WIRE_VALIDATION.md)
using `//robot/board/joshua_wire:joshua_wire_smoke`. The old JW1 probe is removed.
For EtherCAT, build the separate JW profile and follow its configuration and
validation references above; the default UART image contains TI echo EtherCAT.

## Wiring / Pinout

- XDS110 UART console/protocol: 115200 8-N-1; the example uses `/dev/ttyACM0`.
  Check the actual port after connecting the board.
- EtherCAT: existing LP-AM243 IN/OUT ports and TI demo wiring.
- STEP/DIR pins are accepted and retained by the serial protocol but are not
  driven in this milestone.

## Known gaps / Troubleshooting

- In the `ti-demo` dual-transport artifacts, UART0 becomes a binary protocol stream after board initialization. Normal
  EtherCAT application logs are discarded after the serial task starts so
  they cannot corrupt frames; early bootloader logs may still appear before
  the first request and are flushed by Joshua's host transport.
- The dual-transport artifacts expose separate serial and EtherCAT demo state. A later command
  arbiter must unify them before either path controls the same physical motor.
  The separate JW EtherCAT artifact avoids simultaneous UART ownership.
- TI's bundled EtherCAT evaluation stack retains its one-hour runtime limit
  in the `ti-demo` and `jw` artifacts. A replacement requires hardware and
  endurance qualification before retirement.
- No firmware is flashed automatically.
