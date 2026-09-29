# AM243 EtherCAT

The runtime now uses the separate [JoshuaWire v2 EtherCAT profile](../firmware/am243/joshua_dual_transport_v1/README.md#opt-in-jw2-ethercat-profile).
See [configuration](../config/README.md#joshuawire-v2-over-ethercat) and the
[bench results and unresolved timing issue](JOSHUA_WIRE_V2_VALIDATION.md#recorded-am243-ethercat-result--2026-09-28).
The TI-demo host path, codec, driver, smoke targets and preset are retired.
Firmware build/flash assets below remain as historical bring-up material.

## Historical TI-Demo Hardware State

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

The retired TI-demo host path expected the board to enumerate as:

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

## Current Runtime Boundaries

AM243 serial and JW2 EtherCAT both use the shared `JoshuaWireBoard` engine.
Serial v1/v2 support is retained. EtherCAT requires explicit JW2 selection,
`MESSAGE_AND_CYCLIC`, and the matching firmware; old TI-demo configs fail with
migration errors, not automatic protocol conversion.

CommFactory assembles one `EthercatMaster` per NIC, backed by
`SoemEthercatBackend`. Per-slave `JoshuaWireEthercatTransport` endpoints sit
above that master and supply management/cyclic capabilities to the board.
Only the master worker accesses SOEM. Motor drivers see `BoardChannel`,
never a NIC or raw PDO backend.

The JW2 profile uses 80-byte PDOs and CoE management objects, not the TI demo's
8-byte seed/echo mapping. The firmware remains software-channel-only; these
checks do not establish motor-output safety. No replacement hardware test
utility or runnable JW2 preset was added to the repository.

The serial example `config/config_preset/example/am243_serial_demo.pbtxt`
remains. Use the [v2 serial validation guide](JOSHUA_WIRE_V2_VALIDATION.md)
for the maintained serial probe. Firmware flashing is always a separate,
operator-confirmed action.
