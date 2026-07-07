# TI EtherCAT Simple Demo v1

This is the firmware record for the LP-AM243 EtherCAT slave image used by
Joshua's AM243 smoke tests.

## Status

- Type: checked-in vendor demo wrapper
- Board: LP-AM243
- Runtime role: EtherCAT slave for Joshua master-side smoke tests
- Firmware source: `firmware/am243/ti_ethercat_simple_demo_v1`
- Joshua PDO mapping: `AM243_PDO_MAPPING_TI_DEMO`
- Joshua runtime transport: SOEM, split LRD/LWR process data

## Source Provenance

- Checked-in wrapper path:
  `firmware/am243/ti_ethercat_simple_demo_v1`
- Imported from AM243 bring-up commit: `c8a41f3`
- Suggested Joshua tag for shared use: `am243-ethercat-ti-demo-v1`
- Build script:
  `firmware/am243/ti_ethercat_simple_demo_v1/scripts/build_ethercat_simple.sh`
- Flash script:
  `firmware/am243/ti_ethercat_simple_demo_v1/scripts/flash_ethercat_simple.sh`
- Flash config template:
  `firmware/am243/ti_ethercat_simple_demo_v1/setup/ethercat_simple_sbl_ospi.cfg`

The checked-in wrapper contains Joshua-owned scripts, docs, and flash config
templates. It does not contain TI SDK source, Beckhoff SSC source, downloaded
SOEM sources, or generated firmware images.

From the Joshua repo root:

```bash
cd firmware/am243/ti_ethercat_simple_demo_v1
scripts/build_ethercat_simple.sh
scripts/flash_ethercat_simple.sh /dev/ttyACM0
```

## Expected Slave Identity

```text
Name: TI EtherCAT Toolkit for AM243X.R5F
Man:  e000059d
ID:   54490025
Rev:  00010000
PDO:  8 output bytes, 8 input bytes
WKC:  3 with split LRD/LWR
```

## Expected PDO Behavior

- Output byte 0 carries the demo seed.
- Output bytes 1-7 are held at zero by Joshua.
- Input byte 0 echoes the seed one cycle later.
- Input bytes 1-7 are not used by Joshua.

## Known Constraints

- LRW cyclic process data does not work with this firmware/demo. The Joshua
  master side must force split LRD/LWR.
- The SDK-shipped demo uses an evaluation stack. If UART prints
  `EVAL VERSION EXPIRED`, the slave stops responding until the board is reset or
  power-cycled.
- This is not final actuator firmware. Add a new PDO mapping and firmware
  record when real AM243 actuator firmware defines the motor-control contract.
