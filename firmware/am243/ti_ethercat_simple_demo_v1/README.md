# LP-AM243 — TI EtherCAT simple demo (Hello World + EtherCAT slave)

Vendor TI firmware, not Joshua-owned (`docs/BOARD_LAYER_RFC.md` §7.3 —
vendor code stays as-is rather than migrating to the Joshua firmware
pattern). Tracks AM243 setup, firmware experiments, and EtherCAT bring-up
for Joshua. Paired host-side class: `robot/board/am243/am243_board.*`.
See also `firmware/am243/ti_ethercat_simple_demo_v1.md` (the firmware
record: provenance, expected slave identity) and `docs/ethercat.md` (the
detailed EtherCAT bring-up log, including SDK version pitfalls).

## Layout

```text
firmware/am243/ti_ethercat_simple_demo_v1/
  scripts/     build/flash/bring-up helper scripts (see Related files below)
  setup/       SDK import configuration, requirements.txt, flash cfg templates
  docs/
    bringup.md   Hello World bring-up log
    ethercat.md  EtherCAT bring-up log (SOEM master scan, SDK version notes)
```

## Status

- [x] Board recognized, XDS110 debugger working, UART working
- [x] MCU+ SDK build successful
- [x] Hello World executed from CCS and flashed to OSPI; boots after
      reset/power-on without CCS
- [x] LP-AM243 EtherCAT simple slave demo built and flashed (Industrial
      Communications SDK 09); host-side SOEM master reaches OPERATIONAL —
      see `docs/ethercat.md`
- ⬜ Config-driven path (`.pbtxt → actuator_subscriber → Am243Board`)
      proven end to end on this exact firmware image — see
      `docs/BOARD_LAYER_RFC.md` §10 Phase 3's own caveat on this

## Prerequisites

- Hardware: TI LP-AM243 LaunchPad (595-LP-AM243), USB-C cable (power),
  Micro-USB cable (XDS110 debug + UART)
- Toolchain / SDK, exact versions (bring-up is version-sensitive — see
  `docs/bringup.md`/`docs/ethercat.md` for pitfalls with mismatched
  versions):
  - MCU+ SDK: `12.00.00.26`
  - Industrial Communications SDK: `09.00.00.03` (for the EtherCAT demo;
    not needed for Hello World alone)
  - CCS: `21.0.0.00014` (`ccs2100`)
  - SysConfig: `1.17.0` for the Industrial Comms SDK 09 flow, `1.26.3` for
    the plain MCU+ SDK 12 flow — these are NOT interchangeable, see
    `docs/ethercat.md`
  - ARM Clang: `ti-cgt-armllvm_5.1.1.LTS` (ships inside CCS 2100)
  - PRU CGT: `2.3.3`
- Accounts: a free TI (myTI) account to download CCS/SDKs from TI's site.

## Install

```bash
source scripts/activate.sh                                   # create/activate Python venv
python3 -m pip install --upgrade -r setup/requirements.txt   # pyserial, pyelftools, construct, ...
python3 setup/configure_sdk.py                                # patch imports.mak for local TI tool paths
```

For the EtherCAT demo specifically, also run:

```bash
python3 setup/configure_industrial_sdk_09.py
scripts/check_industrial_sdk.sh
```

## Build

```bash
scripts/build.sh                       # Hello World
scripts/build_ethercat_simple.sh       # LP-AM243 EtherCAT slave simple demo (Industrial Comms SDK 09)
scripts/build_ethercat_beckhoff.sh     # LP-AM243 EtherCAT Beckhoff SSC demo (needs external Beckhoff SSC source)
```

## Flash

Set `SW4` before power-cycling the LP-AM243:

```text
UART flashing: SW4 BOOTMODE [1:8] = 11100000
OSPI boot:     SW4 BOOTMODE [1:8] = 01000100
```

In UART flashing mode, the ROM prints `C` every 2-3 seconds on the correct
UART port. Close the UART terminal before flashing.

```bash
scripts/flash.sh /dev/ttyACM0                    # Hello World
scripts/flash_ethercat_simple.sh /dev/ttyACM0     # EtherCAT slave simple demo
```

After a successful flash, switch back to OSPI boot mode and reset the
board.

## Verify

Hello World — verified boot output over UART:

```text
KPI_DATA: [BOOTLOADER_PROFILE] Boot Media       : NOR SPI FLASH
Image loading done, switching to application ...
Hello World!
```

EtherCAT slave demo — verify the board enumerates as an EtherCAT slave and
reaches OPERATIONAL from the host side; see `docs/ethercat.md` for the
full SOEM master scan trace and expected slave identity
(`firmware/am243/ti_ethercat_simple_demo_v1.md` has the expected identity
fields), or run the host smoke binary:

```bash
bazel run //robot/board/am243:am243_config_smoke -- \
  config/config_preset/example/am243_ethercat_demo.pbtxt <interface_name> 80 5000 1
```

## Wiring / Pinout

Not applicable — AM243 communicates over EtherCAT (a NIC-to-NIC Ethernet
link, not a channel-table pinout contract), and this firmware doesn't
expose a Joshua-authored channel table (`docs/BOARD_LAYER_RFC.md` §7.3 —
AM243 stays on its own vendor-specific PDO codec,
`robot/board/am243/am243_pdo_codec.*`).

## Known gaps / Troubleshooting

- The SDK-shipped EtherCAT demo runs on an evaluation stack with a
  runtime limit — if UART prints `EVAL VERSION EXPIRED`, the slave stops
  responding; power-cycle to reset the timer, or rebuild with the
  licensed Beckhoff SSC flow for unlimited runtime.
- SysConfig version is NOT interchangeable between the plain MCU+ SDK 12
  flow (`1.26.3`) and the Industrial Comms SDK 09 flow (`1.17.0`) — using
  the wrong one breaks the build against that SDK's metadata.
- Full troubleshooting/version-pitfall list is in `docs/bringup.md` and
  `docs/ethercat.md`.

## Related files

- `robot/board/am243/am243_board.*` — paired host-side `BoardInterface`
- `robot/board/am243/am243_pdo_codec.*` — AM243-specific PDO byte layout
- `firmware/am243/ti_ethercat_simple_demo_v1.md` — firmware record
  (provenance, expected slave identity)
- `scripts/activate.sh` — create and activate the Python venv
- `scripts/build.sh` / `scripts/build_ethercat_simple.sh` /
  `scripts/build_ethercat_beckhoff.sh` — build Hello World / EtherCAT
  simple demo / EtherCAT Beckhoff SSC demo
- `scripts/flash.sh` / `scripts/flash_ethercat_simple.sh` — flash the
  corresponding image to OSPI over UART
- `scripts/ccs.sh` — launch CCS Theia from the expected install location
- `scripts/check_industrial_sdk.sh` — detect TI Industrial Communications
  SDK and EtherCAT example paths
- `scripts/find_industrial_sdk_artifact.sh` — locate downloaded or
  extracted Industrial Communications SDK artifacts
- `setup/configure_sdk.py` / `setup/configure_industrial_sdk_09.py` —
  configure SDK imports for local TI tool paths

## Notes

The goal of this repo is to treat AM243 as one hardware backend among
many, not as a special-case architecture.
