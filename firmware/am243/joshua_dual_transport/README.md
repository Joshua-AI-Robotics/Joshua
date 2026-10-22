# LP-AM243 — revision-4 JoshuaWire UART + vendor EtherCAT demo

This overlay builds JoshuaWire 0.2.0 / wire revision 4 on UART0 at 115200 baud
alongside TI's existing EtherCAT simple demo. The UART endpoint uses the shared
correlated session, device discovery, actuator profile and software-only backend
in `src/joshua_commands.c`. It produces no STEP/DIR GPIO output. The TI EtherCAT
PDO state remains an independent vendor demo; it is not a second JoshuaWire
transport or a motor-control path.

The earlier JoshuaWire codec/task is removed. Host and firmware must be updated
from the same checkout. Historical flashing/verification of the earlier image
does not verify this incompatible image. Builds and native tests never flash it.

## Prerequisites

Use the Docker development environment with the external TI Industrial
Communications SDK 09.00.00.03, ARM Clang, PRU CGT and SysConfig installed as
described in [the TI guide](../ti_ethercat_simple_demo_v1/README.md). The SDK and
toolchains must be visible inside the container. Build requires `make`, `patch`,
and TI's Python image-generation dependencies.

## Install

Follow the TI guide for SDK/toolchain setup. The build script copies SDK inputs
into a temporary tree, patches the UART task entry point, and enables RNG/SA2UL
in that image's SysConfig for a fresh hardware boot identity. It does not modify
or vendor SDK sources.

## Build

Inside the development container:

```bash
INDUSTRIAL_COMMUNICATIONS_SDK_PATH=/path/to/ind_comms_sdk_am243x_09_00_00_03 \
  firmware/am243/joshua_dual_transport/scripts/build.sh
```

Artifacts are `out/am243_dual_transport_rev4.release.out` and matching `.appimage`
variants. The shared revision-4 codec, stream parser, session, endpoint and runtime
are compiled with the actual TI compiler; no previous codec is linked.

## Flash

Review the built artifact and point the deliberate TI flash operation at
`out/am243_dual_transport_rev4.release.appimage.hs_fs`. Nothing here flashes
automatically. Confirm that this exact image is installed before running host
commands; the host does not downgrade or probe the old protocol.

## Verify

Hardware-free tests exercise the real software command handler and correlated
endpoint through `joshua_wire_session_test`. `firmware_binding_test` also checks
the actual AM243 host binding and serial factory against the physical STEP/DIR
firmware backend with simulated GPIO/clock over a PTY.

After an intentional flash, review the port in
`config/config_preset/example/am243_serial_demo.pbtxt` before running its host
smoke. It uses `message_adapter: JOSHUA_WIRE` and `min_proto_version: 4`.
Verify the vendor EtherCAT PDO sample separately with its low-level smoke.

## Wiring / Pinout

- UART0/XDS110: the intended LP-AM243 setup uses `/dev/ttyACM0`, 115200 8-N-1.
- EtherCAT: TI demo IN/OUT ports and vendor wiring.
- STEP/DIR pins are metadata only in the software actuator; no GPIO is driven.

## Known gaps / Troubleshooting

- UART0 is binary after task initialization; SDK runtime text logs are disabled.
- A single UART task owns the current JoshuaWire session and software actuator.
  The independent TI demo must not be reinterpreted as a correlated JW endpoint.
- Firmware callbacks return cached feedback. The UART loop refreshes its
  command-derived software sample with monotonic timestamps, including while idle.
- A hardware RNG failure prevents the protocol task from starting rather than
  reusing a constant boot identity.
- TI's external evaluation stack retains its vendor runtime restrictions.
- This revision requires separate physical flashing/handshake verification.
