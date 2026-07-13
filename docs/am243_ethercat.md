# AM243 EtherCAT

This document records the current AM243 EtherCAT bring-up state and the Joshua
runtime direction.

## Current Hardware State

- Target board: LP-AM243.
- Firmware: TI EtherCAT simple demo booting from OSPI.
- Validated Linux master interface: `enp5s0`; replace this with the EtherCAT
  NIC name on your machine.
- Detected slave:
  - Name: `TI EtherCAT Toolkit for AM243X.R5F`
  - Output size: 64 bits
  - Input size: 64 bits
  - Manufacturer: `e000059d`
  - Product ID: `54490025`
  - Revision: `00010000`

SOEM can enumerate the slave from Linux. The current AM243 demo firmware does
not respond correctly to LRW cyclic process data frames: SOEM sends `0x0c` LRW
frames, the AM243 demo does not return them, and the master reports WKC `-1`.

Split LRD/LWR process data works with the current firmware:

- `scripts/run_ethercat_soem_split.sh <ethercat_interface>` reaches
  OPERATIONAL. The validated local interface was `enp5s0`.
- Expected working count is WKC `3`.
- PDO seed tests show output byte 0 changing.
- Input byte 0 follows the output seed one cycle behind.

## Board Firmware Requirement

The validated slave firmware wrapper is checked into Joshua under
`firmware/am243/ti_ethercat_simple_demo_v1`. Keep TI SDK sources, generated
firmware images, downloaded SOEM sources, and Beckhoff SSC sources out of
Joshua unless their licenses are explicitly approved for vendoring.

Validated firmware wrapper state:

- Path: `firmware/am243/ti_ethercat_simple_demo_v1`
- Imported from AM243 bring-up commit: `c8a41f3`
- Joshua firmware record:
  `firmware/am243/ti_ethercat_simple_demo_v1.md`
- Demo: TI Industrial Communications SDK EtherCAT slave simple demo,
  device profile `401_simple`
- Boot target: OSPI
- Build script:
  `firmware/am243/ti_ethercat_simple_demo_v1/scripts/build_ethercat_simple.sh`
- Flash script:
  `firmware/am243/ti_ethercat_simple_demo_v1/scripts/flash_ethercat_simple.sh`
- Flash config template:
  `firmware/am243/ti_ethercat_simple_demo_v1/setup/ethercat_simple_sbl_ospi.cfg`

The Joshua side expects the board to enumerate as:

```text
Name: TI EtherCAT Toolkit for AM243X.R5F
Man:  e000059d
ID:   54490025
Rev:  00010000
PDO:  8 output bytes, 8 input bytes
```

Known setup bumps from bring-up:

- MCU+ SDK 12.00.00.26 did not contain the EtherCAT examples used here. The
  working local bring-up used Industrial Communications SDK 09.00.00.03.
- Industrial Communications SDK 09 SysConfig metadata was incompatible with
  SysConfig 1.26.3. Use SysConfig 1.17.0 for that SDK generation flow.
- The PRU compiler installer may create a nested tool root. On the validated
  machine, the usable PRU compiler path was
  `~/ti/ti-cgt-pru_2.3.3/ti-cgt-pru_2.3.3`.
- The SDK docs referenced TI ARM Clang 2.1.3.LTS, but that download URL
  resolved to a TI 404 page during bring-up. The SDK-shipped `401_simple` demo
  was verified locally with CCS 21 ARM Clang 5.1.1.
- Start with `ethercat_slave_demo/device_profiles/401_simple`, not
  `ethercat_slave_beckhoff_ssc_demo`. The Beckhoff SSC demo requires external
  Beckhoff/ETG SSC 5i13 source; without it, the build fails on missing
  `ecat_def.h`.
- The flash config for the simple demo uses the Industrial Communications SDK
  09 SBL and `.appimage.hs_fs` application image format, not the MCU+ SDK 12
  Hello World `.mcelf.hs_fs` format.
- The SDK-shipped EtherCAT simple demo uses an evaluation stack. If UART prints
  `EVAL VERSION EXPIRED`, the EtherCAT slave stops responding and SOEM will no
  longer connect. Power-cycle or reset the LP-AM243 to restart the demo timer,
  or move to TI's licensed Beckhoff SSC flow for unlimited runtime.

## Runtime Direction

Joshua should use EtherCAT PDOs as the runtime actuator path. UART or serial
links may remain useful for flashing, board management, logs, and debug, but
they should not become the runtime actuator transport for AM243-backed motors.

For the current AM243 firmware/demo, the Linux/SOEM master must force split
LRD/LWR process data cycles. Do not retry LRW unless the board firmware or
EEPROM/ESI configuration changes.

Joshua enforces this in `Am243Board::Init`: LRW process-data mode is rejected
before the SOEM bring-up starts.

Generic EtherCAT working-count validation lives in
`robot/comm/ethercat/ethercat_status.*`; AM243 runs should expect WKC `3` with
the current split LRD/LWR demo path.

## Repository Boundaries

AM243 support follows the board layer (docs/BOARD_LAYER_RFC.md):

- Generic EtherCAT transport belongs in `robot/comm/ethercat/`. The SOEM
  master is cached per interface name by `robot/comm/factory/comm_factory.*`
  — one master per NIC, shared by every board on that interface.
- The AM243 board controller belongs in `robot/board/am243/am243_board.*`.
  Its `Init` owns the full SOEM lifecycle (`ConfigureSlaves` -> `StartCyclic`
  -> verify OPERATIONAL), enforces split LRD/LWR, resolves the slave's PDO
  region, and hands out `BoardChannel`s; every exchange is working-count
  checked.
- The AM243 PDO byte layout belongs in `robot/board/am243/am243_pdo_codec.*`.
- Motor semantics belong in `robot/action/motors/drivers/ti_demo_driver.*`
  (`MOTOR_TI_DEMO`): limits, idle position, and the joint->native
  conversion, over `BoardChannel` with no comm or board headers.
- Board-management tooling, including UART flashing and debug helpers, should
  stay outside the runtime actuator path.
- AM243 boards use `comm_type: ETHERCAT` with
  `ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR` while running the current TI demo
  firmware, and must set `pdo_mapping: AM243_PDO_MAPPING_TI_DEMO`. This keeps
  the validated TI demo byte-walk separate from any future actuator firmware
  PDO contract.

An example config lives at
`config/config_preset/example/am243_ethercat_demo.pbtxt`: a `boards{}` entry
declares the AM243 slave and its `PDO_JOINT` channel, and the actuator binds
via `motor_type: MOTOR_TI_DEMO` + `board_name` + `channel`.

The backend pins upstream SOEM v2.0.0 in Bazel and builds a SOEM-backed
transport. `Init()` opens the SOEM master socket in split LRD/LWR mode,
`ConfigureSlaves()` discovers slaves, forces SOEM's `blockLRW` path, maps PDO
regions, and `Teardown()` closes the socket. `StartCyclic()` transitions slaves
through SAFE-OP to OPERATIONAL, and `ExchangeProcessData()` uses SOEM's split
LRD/LWR process-data path.

## Hardware Smoke Test

With the LP-AM243 connected to the Linux EtherCAT NIC, run the low-level demo
PDO smoke test from the Ubuntu 24.04 + ROS 2 Jazzy container:

```bash
docker compose exec joshua-u24 bazel run //robot/comm/ethercat:am243_demo_smoke -- <ethercat_interface> 20 1
```

The tool opens the interface, configures slaves, starts cyclic exchange, writes
the AM243 demo seed in output byte 0 with output bytes 1-7 held at zero, and
prints the working count plus input byte 0. With the current TI demo firmware,
expect WKC `3` and input byte 0 to follow the output seed one cycle behind.

To test the Joshua board + driver path, run:

```bash
docker compose exec joshua-u24 bazel run //robot/board/am243:am243_driver_smoke -- <ethercat_interface> 80 5000 1
```

This brings up `Am243Board` (which owns the SOEM lifecycle), opens its
`PDO_JOINT` channel, and drives it with `TiDemoDriver` `ActionPacket` position
commands. With the current TI demo firmware, input byte 0 should trail the
generated command seed by one cycle.

To test the full config-driven path — the same resolution the
actuator_subscriber node runs — use:

```bash
docker compose exec joshua-u24 bazel run //robot/board/am243:am243_config_smoke -- config/config_preset/example/am243_ethercat_demo.pbtxt <ethercat_interface> 80 5000 1
```

This loads `config/config_preset/example/am243_ethercat_demo.pbtxt`, applies
the optional interface and slave-index overrides, resolves the actuator
through `ActionFactory -> BoardFactory -> Am243Board`, and then sends the same
Joshua `ActionPacket` position commands.

For example, if the EtherCAT NIC is `enp5s0`, use `enp5s0` in place of
`<ethercat_interface>`.

## Current PDO Codec Scope

The only AM243 PDO byte-level behavior encoded in Joshua today is the validated
TI simple-demo walk:

- Output PDO size: 8 bytes.
- Input PDO size: 8 bytes.
- Output byte 0 carries the command seed.
- Output bytes 1-7 are held at zero because their TI demo contents do not
  represent Joshua actuator fields.
- Input byte 0 echoes that seed one cycle later.

This is captured as a demo codec and is available only when
`pdo_mapping: AM243_PDO_MAPPING_TI_DEMO` is selected. Position, speed, and torque
are mapped to a demo seed for smoke testing; this is not the final actuator
command map. Add a new `Am243PdoMapping` value when AM243 firmware defines a real
actuator PDO layout.
