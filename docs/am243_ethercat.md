# AM243 EtherCAT

This document records the current AM243 EtherCAT bring-up state and the Joshua
runtime direction.

## Current Hardware State

- Target board: LP-AM243.
- Firmware: TI EtherCAT simple demo booting from OSPI.
- Linux master interface: `enp5s0`.
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

- `scripts/run_ethercat_soem_split.sh enp5s0` reaches OPERATIONAL.
- Expected working count is WKC `3`.
- PDO walking shows output bytes changing.
- Input byte 0 follows the output seed one cycle behind.

## Runtime Direction

Joshua should use EtherCAT PDOs as the runtime actuator path. UART or serial
links may remain useful for flashing, board management, logs, and debug, but
they should not become the runtime actuator transport for AM243-backed motors.

For the current AM243 firmware/demo, the Linux/SOEM master must force split
LRD/LWR process data cycles. Do not retry LRW unless the board firmware or
EEPROM/ESI configuration changes.

## Repository Boundaries

AM243 support should fit into Joshua's existing robot architecture:

- Generic EtherCAT transport belongs in `robot/comm/ethercat/`.
- AM243-specific actuator mapping belongs in
  `robot/action/motors/drivers/am243_ethercat_driver.*`.
- Board-management tooling, including UART flashing and debug helpers, should
  stay outside the runtime actuator path.

The first backend skeleton intentionally does not add a SOEM implementation.
The next implementation step is to introduce a generic EtherCAT transport API
that can run split LRD/LWR cyclic PDO exchange and expose process-data buffers
to actuator drivers.
