# LP-AM243 — Joshua dual EtherCAT + serial firmware v1

One AM243 image that keeps TI's EtherCAT simple demo active while also serving
`joshua_wire_v1` on UART0 at 115200 baud. The build layers Joshua-owned source
and a small patch over the externally installed TI Industrial Communications
SDK; it does not modify or vendor the SDK.

The LP-AM243 image implements dual EtherCAT and serial transports for testing
and verification. The image has been built and flashed on the LP-AM243, the
serial protocol (IDENTIFY, CONFIGURE, ENABLE, SET_TARGET, GET_FEEDBACK) has
been exercised, and EtherCAT reaches OPERATIONAL with the serial task active.
This milestone is intentionally motion-safe: the serial channel reports
`STEP_DIR` and implements the command/response protocol in software but does
not drive STEP/DIR GPIOs or move motors.

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
firmware/am243/joshua_dual_transport_v1/scripts/build.sh
```

Artifacts are written under the ignored `out/` directory. The build uses a
temporary working tree and leaves TI SDK sources unchanged.

For the opt-in v2 **UART** artifact, build with:

```bash
JOSHUA_WIRE_VERSION=2 firmware/am243/joshua_dual_transport_v1/scripts/build.sh
```

Outputs are named `am243_dual_transport_v2.release.*`. The script also forwards
arguments to `make`, allowing explicit `CCS_PATH`, `SYSCFG_PATH`, and compiler
path overrides without editing the SDK. EtherCAT remains the TI demo, not
JoshuaWire v2. The software-only UART command handler is shared with native
host/session tests. V2 was flashed and UART-validated on LP-AM243 on 2026-09-27:
eight sessions covered reset, identity, configuration, software targets/feedback,
disable and ESTOP. See the [recorded validation](../../../docs/JOSHUA_WIRE_V2_VALIDATION.md#recorded-am243-hardware-result--2026-09-27)
for the exact artifact and scope; EtherCAT and physical motion were not tested.
See the
[v2 milestone and safety limits](../../README.md#opt-in-joshuawire-v2-serial-milestone).

### Opt-in JW2 EtherCAT profile

The same overlay also builds a separate **EtherCAT-only** JoshuaWire v2 artifact:

```bash
JOSHUA_WIRE_VERSION=2 JOSHUA_ETHERCAT_PROFILE=jw2 \
JOSHUA_COMM_WATCHDOG_US=500000 JOSHUA_TARGET_WATCHDOG_US=250000 \
firmware/am243/joshua_dual_transport_v1/scripts/build.sh
```

Those intervals are software-demo examples, not validated motor-safety limits.
Both must be explicit (10000–999999999 microseconds); there are no watchdog
defaults. Outputs are `out/am243_ethercat_jw2.release.*`. The default profile
remains `ti-demo`, preserving both existing UART artifacts and TI's echo PDOs.
The JW2 profile starts no UART protocol task; the console remains available for
SDK logs. It has one software-only channel and **no STEP/DIR GPIO backend**.

Source responsibilities:

- `src/joshua_commands.{h,c}`: transport-neutral software channel operations,
  shared with the UART artifacts (formerly `joshua_serial_commands`).
- `src/joshua_ethercat_profile.{h,c}`: portable object/PDO protocol, one session
  across CoE and PDO, retained replies, and latched software watchdogs.
- `src/joshua_ethercat_ti.{h,c}`: SDK object registration, complete image copies,
  callback serialization and an independent RTOS watchdog task.
- `patches/jwec_profile.patch`: replaces TI's demo object/PDO setup and dispatch
  in a temporary SDK source copy. TI demo EEPROM-persistence callbacks are not
  installed for this profile; the SDK builds the profile from its new mapping.

The descriptor reports `am243-ec-v2`, JW2/layout-v1, 80-byte input/output images
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
the host's paired CoE/PDO adapters. This profile was flashed and bench-tested on
LP-AM243 on 2026-09-28: discovery, factory/engine commands, software feedback,
stale-target latching and fresh-session recovery passed. One 1 ms host mailbox
deadline failure was also observed; broader timing validation remains open.
See the [artifact, results and limits](../../../docs/JOSHUA_WIRE_V2_VALIDATION.md#recorded-am243-ethercat-result--2026-09-28).
Software watchdog behavior is not
proof of physical motor safety, CPU-halt coverage or hard-real-time timing.

## Host integration

The [JW2 EtherCAT config](../../../config/README.md#joshuawire-v2-over-ethercat)
now selects paired adapters through CommFactory and the shared board engine.
Native integration tests run that entire path against this production firmware
core. The recorded single-board bench check also exercises SDK callbacks, but
does not establish production Ethernet timing or physical-output safety.

## Flash

Not automated by this target. After the image is built and reviewed, adapt the
existing TI demo flash configuration to point at:

```text
out/am243_dual_transport_v1.release.appimage.hs_fs
```

For explicit v2, use `out/am243_dual_transport_v2.release.appimage.hs_fs`
instead, with the same SDK 09 bootloader/application offsets. Verify both
bootloader and application after flashing; do not use the TI-demo application
path from the original flash template for a JoshuaWire image.

The separate JW2 EtherCAT profile uses
`out/am243_ethercat_jw2.release.appimage.hs_fs`; it is not the UART-v2 image.

Flashing remains a deliberate hardware operation and must not happen as part
of build or test.

## Verify

For v2, follow the [serial validation guide](../../../docs/JOSHUA_WIRE_V2_VALIDATION.md)
using `joshua_wire_v2_smoke`. The commands below apply to **v1 only**.

After an intentional v1 flash, first run the serial protocol smoke without motor
movement:

```bash
bazel run //robot/comm/serial:am243_demo_smoke -- /dev/ttyACM0 10 0 250
```

Example output from the LP-AM243 dual-transport image:

```text
serial port=/dev/ttyACM0 baud=115200 protocol=joshua_wire_v1/1
board id=1 firmware="am243-dual-v1" channels=1
channel 0 drive=STEP_DIR
configure channel=0 step=2 dir=3 enable=4: OK
enable channel=0: OK
cycle=0 target=500.0 position=500.0 velocity=0.0 faults=0x0000 roundtrip_us=5418
cycle=1 target=-500.0 position=-500.0 velocity=0.0 faults=0x0000 roundtrip_us=5960
disable channel=0: OK
```

The former TI-demo host EtherCAT smoke/codec path is retired. To use current
Joshua EtherCAT runtime, explicitly build and flash the separate JW2 profile
and follow its config/validation references above; UART-v1/v2 artifacts still
contain TI echo firmware, not JW2 EtherCAT.

## Wiring / Pinout

- XDS110 UART console/protocol: `/dev/ttyACM0`, 115200 8-N-1 on the verified
  LP-AM243 setup.
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
  The separate JW2 EtherCAT artifact avoids simultaneous UART ownership.
- TI's bundled EtherCAT evaluation stack retains its one-hour runtime limit.
- No firmware is flashed automatically.
