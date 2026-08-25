# LP-AM243 — Joshua dual EtherCAT + serial firmware v1

One AM243 image that keeps TI's EtherCAT simple demo active while also serving
`joshua_wire_v1` on UART0 at 115200 baud. The build layers Joshua-owned source
and a small patch over the externally installed TI Industrial Communications
SDK; it does not modify or vendor the SDK.

## Status

- [x] Source overlay and isolated build flow defined
- [x] Image built successfully
- [ ] Flashed to LP-AM243
- [ ] Serial IDENTIFY/CONFIGURE/ENABLE/SET_TARGET/GET_FEEDBACK verified
- [ ] EtherCAT reaches OPERATIONAL with the serial task active
- [ ] Physical motor output implemented

This first milestone is deliberately motion-safe. Serial channel 0 reports
`STEP_DIR` and implements the full command/response protocol in software, but
does not toggle GPIO. EtherCAT retains the TI demo's existing PDO behavior.

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

## Flash

Not automated by this target. After the image is built and reviewed, adapt the
existing TI demo flash configuration to point at:

```text
out/am243_dual_transport_v1.release.appimage.hs_fs
```

Flashing remains a deliberate hardware operation and must not happen as part
of build or test.

## Verify

After an intentional flash, first run the serial protocol smoke without motor
movement:

```bash
bazel run //robot/board/am243:am243_driver_smoke -- /dev/ttyACM0
```

Then separately verify EtherCAT with the retained low-level smoke:

```bash
bazel run //robot/comm/ethercat:am243_demo_smoke -- <interface_name> 20 1
```

## Wiring / Pinout

- XDS110 UART console/protocol: `/dev/ttyACM0`, 115200 8-N-1 on the verified
  LP-AM243 setup.
- EtherCAT: existing LP-AM243 IN/OUT ports and TI demo wiring.
- STEP/DIR pins are accepted and retained by the serial protocol but are not
  driven in this milestone.

## Known gaps / Troubleshooting

- UART0 becomes a binary protocol stream after board initialization. Normal
  EtherCAT application logs are discarded after the serial task starts so
  they cannot corrupt frames; early bootloader logs may still appear before
  the first request and are flushed by Joshua's host transport.
- Serial and EtherCAT currently expose separate demo state. A later command
  arbiter must unify them before either path controls the same physical motor.
- TI's bundled EtherCAT evaluation stack retains its one-hour runtime limit.
- No firmware is flashed automatically.
