# AM243 Bring-up for Joshua

This repo tracks AM243 setup, firmware experiments, and EtherCAT bring-up for Joshua.

Verified setup:
- Board: LP-AM243 / 595-LP-AM243
- MCU+ SDK: 12.00.00.26
- CCS: 21.0.0.00014
- SysConfig: 1.26.3
- OSPI boot: Hello World verified after reset/power-on without CCS

## Workflow

Use the repository helper scripts and setup automation to keep the AM243 toolchain reproducible.

1. Activate the virtual environment:
   ```bash
   source scripts/activate.sh
   ```
2. Install required Python packages:
   ```bash
   python3 -m pip install --upgrade -r setup/requirements.txt
   ```
3. Configure the TI SDK imports file:
   ```bash
   python3 setup/configure_sdk.py
   ```
4. Build the Hello World example:
   ```bash
   scripts/build.sh
   ```
5. Flash the OSPI image for boot-on-power:
   ```bash
   scripts/flash.sh /dev/ttyACM0
   ```

## Boot Modes

Set `SW4` before power-cycling the LP-AM243.

```text
UART flashing: SW4 BOOTMODE [1:8] = 11100000
OSPI boot:     SW4 BOOTMODE [1:8] = 01000100
```

In UART flashing mode, the ROM prints `C` every 2-3 seconds on the correct UART port. Close the UART terminal before running `scripts/flash.sh`.

After a successful flash, switch back to OSPI boot mode and reset the board. Verified boot output:

```text
KPI_DATA: [BOOTLOADER_PROFILE] Boot Media       : NOR SPI FLASH
Image loading done, switching to application ...
Hello World!
```

## Scripts

- `scripts/activate.sh` — create and activate the Python venv
- `scripts/build.sh` — build the Hello World firmware
- `scripts/build_ethercat_simple.sh` — build the LP-AM243 EtherCAT slave simple demo from Industrial Communications SDK 09
- `scripts/build_ethercat_beckhoff.sh` — build the LP-AM243 EtherCAT Beckhoff SSC demo
- `scripts/flash.sh` — flash the firmware to OSPI over UART
- `scripts/flash_ethercat_simple.sh` — flash the EtherCAT slave simple demo to OSPI over UART
- `scripts/ccs.sh` — launch CCS Theia from the expected install location
- `scripts/check_industrial_sdk.sh` — detect TI Industrial Communications SDK and EtherCAT example paths
- `scripts/find_industrial_sdk_artifact.sh` — locate downloaded or extracted Industrial Communications SDK artifacts
- `setup/configure_industrial_sdk_09.py` — configure Industrial Communications SDK 09 imports for local TI tool paths

## Milestones

- [AM243 bring-up](docs/bringup.md)
- [AM243 EtherCAT](docs/ethercat.md)

## Notes

The goal of this repo is to treat AM243 as one hardware backend among many, not as a special-case architecture.
