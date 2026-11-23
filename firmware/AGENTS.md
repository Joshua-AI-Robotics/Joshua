# Firmware

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../AGENTS.md#coding-guidelines). This file owns the rules and architecture
contract for `firmware/`; child directories inherit it unless they add local rules.

## Architecture contract

Shared codecs, session state, and portable dispatch belong in `common/`;
board adapters own platform I/O, GPIO mapping, and build artifacts. Reuse the
portable core across boards rather than forking wire formats. Host bindings
remain in `robot/board` and `robot/comm`, with a compatible checked-in protocol.
The normative [JoshuaWire contract](../docs/JOSHUA_WIRE.md) owns wire semantics;
directory guides describe board builds, wiring, and qualification limits.

Preserve exact-retry, session/reset, fault/ESTOP, and explicit recovery behavior.
Protocol callbacks need bounded work and explicit resource staging/rollback.
Changes to wire layouts require compatible schema/version handling, host tests,
and golden bytes. Keep simulated output state distinct from measured motion.

## Verification

Use native firmware/host interoperability tests with controlled clocks and fake
GPIO/serial backends, then board cross-builds when applicable. Cross-builds and
native tests do not qualify physical timing or stopping. Never flash, recover,
or enable a board as part of automated verification; follow root authorization
and the board AGENTS.md for deliberate hardware work.

## Joshua Firmware

Joshua-owned MCU firmware and host bindings use JoshuaWire **0.2.0 / wire
revision 4** from the same checkout. Earlier JoshuaWire implementations are
removed. Existing devices need an intentional firmware update before use with
this host; neither runtime initialization nor tests flash hardware.

## Firmware records

- Joshua-owned firmware source and shared codecs that can be built from this
  repository.
- Metadata for vendor or bring-up firmware that Joshua can validate against,
  but should not vendor until redistribution rights are clear.

Runtime code must not flash boards automatically. Flashing is a board-management
operation described by the board guide. Runtime board initialization should
detect a mismatch and report the
firmware/artifact needed to fix it.

## How to flash a board

Start with the board guide below, select one firmware variant, build it, identify
the exact connected board, and review the upload command. Building, preparing a
flash configuration, or running tests never authorizes uploading. Follow the
[root hardware authorization](../AGENTS.md#hardware-safety--read-this-first).

| Board | Authoritative flashing guide |
| --- | --- |
| LP-AM243 HS-FS | [AM243 board guide](am243/AGENTS.md) — UART, TI-stack JW EtherCAT, SOES, and legacy vendor images |
| Teensy 4.1 | [Teensy guide](teensy/41/AGENTS.md) — request-only and mixed-scalar images |
| ESP32 DevKitC / esp32dev | [ESP32 guide](esp32/AGENTS.md) — UART image |
| Arduino | [Arduino guide](arduino/AGENTS.md) — no image or upload target exists |

### Toolchain environment

Run repository commands from `/workspace` in the Docker development shell; a
CPU-only firmware shell is sufficient:

```bash
docker compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm joshua-u22
```

For PlatformIO boards, use the reference CLI version and record resolved packages:

```bash
python3 -m pip install platformio==6.1.18
pio --version
```

The default Teensy and ESP32 platform declarations are not pinned. Record their
resolved platform/framework versions with `pio pkg list -d <project> -e <env>`
and the artifact SHA256; do not infer reproducibility from the CLI version alone.
The Teensy mixed-scalar environment pins its platform separately. TI SDKs and
toolchains are external inputs and must be mounted into the container at the
paths used by their make imports; the AM243 guide lists the required versions.

For an authorized hardware session, expose only the intended devices to that
shell and verify their identity again inside it. ESP32 needs its USB-UART device;
Teensy programming also needs USB bootloader access; AM243 needs the selected
XDS110 UART. A container-visible device name alone does not identify a board.
Close terminals and other owners before uploading.

## Required board-guide format

This section is the canonical template. Put board-specific values in the board
`AGENTS.md`, not in another template, script comment, or copied platform guide.
Implementation subdirectories inherit the board procedure and document only
variant-specific source/build details.

1. **Identity and status:** exact board/revision, supported MCU/security variant,
   host binding, and what has actually been built, flashed, and measured.
2. **Firmware selection:** explicit build environment/profile, transport/protocol,
   artifact path, and incompatible or legacy images.
3. **Prerequisites and install:** supported container, tool/SDK versions, accounts,
   packages, permissions, and external mounts. State unresolved dependencies.
4. **Build:** exact working directory and build-only commands; list outputs and
   how to record hashes and resolved tool versions.
5. **Preparation:** board/device identity, power and wiring conditions, boot
   switches/buttons, exclusive device access, and root hardware authorization.
6. **Flash:** explicit selected artifact/device and complete upload command or
   generated configuration, including bootloader/offset requirements where needed.
7. **Return to normal boot:** switch restoration, reset/power cycle, and device
   re-identification. Upload completion is not proof the application booted.
8. **Verify:** enumeration and identity, then an explicitly authorized protocol
   handshake. Keep configuration writes and powered motion separate.
9. **Recovery and troubleshooting:** wrong device/image, missing permissions,
   stuck boot mode, failed transfers, and known unsupported combinations.
10. **Evidence and limitations:** commit, artifact/hash, board ID/revision, tool
    versions, operation/commands, power state, result/log, and remaining gaps.

For an unsupported board, say that no image/command exists; never fill a section
with an invented operation. Keep wiring facts in protobuf and board-specific
limits, and link shared protocol and authorization rules. Native tests, static
config checks, build success, flash verification, protocol acknowledgment, and
physical motion qualification are distinct evidence levels.

## Current firmware records

JoshuaWire (JW) version `0.2.0` is the sole Joshua protocol. JW1 is removed;
former JW2 code, APIs and build targets now use the unversioned JW name.
The validated wire revision is `4`, including CRC, session/message IDs,
command payloads and EtherCAT layout. Vendor TI-demo metadata remains separate.

| Board/path | Implementation | AGENTS.md |
| --- | --- | --- |
| AM243 UART | Software channel; no GPIO output | [AM243](am243/joshua_dual_transport/AGENTS.md) |
| AM243 JW EtherCAT, TI stack | Software channel with watchdogs; no GPIO output | [AM243 EtherCAT profile](am243/joshua_dual_transport/AGENTS.md#opt-in-jw-ethercat-profile) |
| Teensy 4.1 serial | STEP/DIR GPIO backend; native USB serial | [Teensy](teensy/41/AGENTS.md) |
| ESP32 serial | STEP/DIR GPIO backend; UART/USB bridge | [ESP32](esp32/AGENTS.md) |
| Arduino | Not started | [Arduino](arduino/AGENTS.md) |
| AM243 vendor TI EtherCAT demo | Historical vendor bring-up metadata | [TI demo](am243/ti_ethercat_simple_demo_v1/AGENTS.md) |

## JoshuaWire serial

Build without flashing:

```bash
pio run -d firmware/teensy/41 -e teensy41-serial
pio run -d firmware/esp32 -e esp32-serial
firmware/am243/joshua_dual_transport/scripts/build.sh
```

Select `protocol: JOSHUA_WIRE` and `firmware { min_proto_version: 4 }` in the
host Board config. Omitted protocol selects JW for Joshua boards; vendor boards
retain their own protocol. There is no auto-detection or fallback to JW1.
Existing explicit `JOSHUA_WIRE_V2` configs must use `JOSHUA_WIRE`.

Use the [serial validation procedure](../docs/JOSHUA_WIRE_VALIDATION.md)
for operator-confirmed hardware checks.
The AM243 default image serves JW on UART; EtherCAT still runs TI's demo with
separate state and no physical motor output. Use the separate JW EtherCAT
artifact below for Joshua EtherCAT runtime.

JW requires RESET_SESSION before commands. Reset disables existing outputs
before clearing configuration; the host must configure and explicitly enable
channels again. ESTOP survives a new session; explicit RECOVER is required to clear it. The serial firmware session
executes strictly increasing message IDs, replays the cached response for an
identical immediate retry, and drops older IDs or changed requests reusing an ID.
Session IDs provide correlation, not authentication. The default serial images
have no communication-loss watchdog. The opt-in Teensy
[`teensy41-mixed-scalar`](teensy/41/AGENTS.md) image adds explicitly configured
communication and target watchdogs with scalar acquisition. Its software tests
and cross-build do not establish physical timing or stopping qualification.
Simultaneous transport ownership still requires an arbiter.

Native Bazel tests exercise the actual MCU dispatch with simulated serial/GPIO,
the AM243 software handler and host sessions. Tests never flash or move hardware.

## Opt-in AM243 JoshuaWire EtherCAT milestone

The [AM243 overlay](am243/joshua_dual_transport/AGENTS.md#opt-in-jw-ethercat-profile)
also builds the explicit `am243_ethercat_jw` artifact. It replaces TI's echo
profile with JW CoE management and 112-byte PDO command/feedback images, one
shared session, retained responses and latched software watchdogs. UART protocol
service is absent from this artifact; the UART/TI-demo image is built separately.

The endpoint and watchdog policy now live in `common/joshua_ethercat_profile`.
AM243 supplies identity, its TI-stack bridge and drive callbacks. Other boards
reuse that core and the same host communication implementation; see the
[firmware porting contract](common/AGENTS.md#porting-jw-ethercat-to-another-board).
The original AM243 images still link TI's one-hour evaluation stack. A separate
[SOES candidate](am243/joshua_dual_transport/AGENTS.md#opt-in-soes-replacement)
builds without that stack and has native protocol coverage. Hardware timing
and endurance qualification remain open, including the
[master-side NIC timing issue](../robot/comm/ethercat/AGENTS.md#known-master-side-nic-timing-issue).
Evaluation-stack retirement remains pending.

The production firmware core has native host-interoperability and controlled-clock
tests. There is still no physical motor backend or simultaneous transport
arbitration. Native coverage does not establish production timing or motor safety.
Explicit watchdog intervals are required at build time; see the profile AGENTS.md
for the mapping, build command and safety limits. No flashing is automatic.

## Source layout

For the protocol/session/dispatch file map and contributor entry points, see
[Shared firmware and JoshuaWire](common/AGENTS.md).

```text
firmware/
  AGENTS.md     shared workflow, board index, and required guide format
  common/       # shared commands, JW codec, sessions and drive backend
  testing/      # native-test Arduino substitute and shared firmware tests
  am243/        # TI demo metadata plus Joshua's dual-transport source overlay
  teensy/41/    # Joshua-owned firmware for the Teensy 4.1
  arduino/      # not started — placeholder AGENTS.md only
  esp32/        # Joshua-owned UART STEP/DIR firmware
```

Firmware variants should be explicit build artifacts. A board may support more
than one transport in a single image when the transports can coexist safely;
artifact names still identify the board, transport set, and protocol version.

## Directory navigation

- [am243/](am243/AGENTS.md)
- [arduino/](arduino/AGENTS.md)
- [common/](common/AGENTS.md)
- [esp32/](esp32/AGENTS.md)
- [teensy/41/](teensy/41/AGENTS.md)
