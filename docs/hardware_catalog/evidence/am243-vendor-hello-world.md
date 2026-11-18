# LP-AM243 Bring-up Notes

## Objective

Bring up the TI LP-AM243 LaunchPad and establish a reproducible development environment for Joshua.

Status:

- ✅ Board recognized
- ✅ XDS110 debugger working
- ✅ UART working
- ✅ MCU+ SDK build successful
- ✅ Hello World executed from CCS
- ✅ Hello World flashed to OSPI
- ✅ Hello World boots after reset/power-on without CCS

---

# Hardware

Board

- TI LP-AM243 LaunchPad (595-LP-AM243)

Connections

- USB-C → Power
- Micro-USB → XDS110 Debug + UART

UART

```
/dev/ttyACM0
```

Boot modes

```
UART flashing: SW4 BOOTMODE [1:8] = 11100000
OSPI boot:     SW4 BOOTMODE [1:8] = 01000100
```

USB Device

```
0451:bef3 Texas Instruments XDS110
```

---

# Software Versions

| Component | Version |
|-----------|---------|
| Ubuntu | 24.04 |
| MCU+ SDK | 12.00.00.26 |
| CCS | 21.0.0.00014 |
| SysConfig | 1.26.3 |
| ARM LLVM | 5.1.1.LTS |

---

# Required Linux Packages

```bash
sudo apt install \
    libusb-0.1-4 \
    libc6-i386 \
    libpython3.9 \
    python3-pyelftools \
    python3-construct
```

---

# CCS Installation

Installed at

```
~/ti/ccs2100
```

Executable

```bash
~/ti/ccs2100/ccs/theia/ccstudio
```

---

# SysConfig

Installed at

```
~/ti/sysconfig_1.26.3
```

---

# MCU+ SDK

Installed at

```
~/ti/mcu_plus_sdk_am243x_12_00_00_26
```

---

# imports.mak modifications

Linux section

```make
export TOOLS_PATH?=$(HOME)/ti
export CCS_PATH?=$(TOOLS_PATH)/ccs2100/ccs
```

SysConfig

```make
SYSCFG_PATH ?= $(TOOLS_PATH)/sysconfig_1.26.3
```

Compiler

```make
CGT_TI_ARM_CLANG_PATH=$(CCS_PATH)/tools/compiler/ti-cgt-armllvm_5.1.1.LTS
```

---

# Python Dependencies

MCU+ SDK image generation requires

- pyelftools
- construct

Install

```bash
sudo apt install python3-pyelftools python3-construct
```

---

# CCS Theia Fix

Required on Ubuntu

```bash
sudo chown root:root ~/ti/ccs2100/ccs/theia/chrome-sandbox
sudo chmod 4755 ~/ti/ccs2100/ccs/theia/chrome-sandbox
```

---

# XDS110

CCS automatically upgraded firmware

```
3.0.0.15
    ↓
3.0.0.43
```

---

# Build

```bash
cd ~/ti/mcu_plus_sdk_am243x_12_00_00_26

make -C examples/hello_world/am243x-lp/r5fss0-0_nortos/ti-arm-clang all
```

Expected output

```
Boot image ... hello_world.release.mcelf.hs_fs Done !!!
```

---

# Setup workflow

```bash
cd firmware/am243/ti_ethercat_simple_demo_v1
source scripts/activate.sh
python3 -m pip install --upgrade -r setup/requirements.txt
python3 setup/configure_sdk.py
scripts/build.sh
```

# Flash OSPI for Boot-on-Power

After building Hello World, flash the OSPI image so the board boots the app after power-on without CCS.

1. Power off the LP-AM243.
2. Set UART flashing boot mode:

   ```
   SW4 BOOTMODE [1:8] = 11100000
   ```

3. Power on the LP-AM243.
4. Confirm the ROM is in UART boot mode. The correct UART port prints `C` every 2-3 seconds:

   ```bash
   picocom -b 115200 /dev/ttyACM0
   ```

   Exit picocom before flashing:

   ```
   Ctrl+A, then Ctrl+X
   ```

5. Flash the OSPI bootloader and Hello World application:

```bash
cd firmware/am243/ti_ethercat_simple_demo_v1
scripts/flash.sh /dev/ttyACM0
```

The flash script uses `uart_uniflash.py` from the TI SDK and the `setup/hello_world_sbl_ospi.cfg` configuration.

Expected final flash output

```
All commands from config file are executed !!!
```

After flashing:

1. Power off the LP-AM243.
2. Set OSPI boot mode:

   ```
   SW4 BOOTMODE [1:8] = 01000100
   ```

3. Power on or reset the board.
4. Open the UART console:

   ```bash
   picocom -b 115200 /dev/ttyACM0
   ```

---

# Debug

Open project in CCS.

Run

```
Run → Start Debugging
```

Program downloads through XDS110.

---

# Verification

Observed output

```
MAIN_Cortex_R5_0_0: Hello World!
```

Observed boot-from-OSPI output

```
DMSC Firmware Version 12.0.2--v12.00.02
KPI_DATA: [BOOTLOADER_PROFILE] Boot Media       : NOR SPI FLASH
Image loading done, switching to application ...
Hello World!
```

This confirms

- JTAG working
- XDS110 working
- Compiler working
- Linker working
- Image generation working
- Download working
- UART working
- OSPI flashing working
- OSPI boot working

---

# Known Issues

## UART Flashing Stuck at 0%

If `uart_uniflash.py` shows the flash writer transfer stuck at `0%`, the board is usually not in UART flashing boot mode or the wrong `/dev/ttyACM*` port is being used.

Checks:

- Set UART flashing boot mode:

  ```text
  SW4 BOOTMODE [1:8] = 11100000
  ```

- Power-cycle after changing BOOTMODE.
- Confirm the ROM is alive. The correct UART port prints `C` every 2-3 seconds at 115200 baud:

  ```bash
  picocom -b 115200 /dev/ttyACM0
  ```

- Close `picocom` before running a flash script. Only one process can own `/dev/ttyACM0`.
- If output is missing, check both ACM ports exposed by XDS110:

  ```bash
  ls /dev/ttyACM*
  picocom -b 115200 /dev/ttyACM0
  picocom -b 115200 /dev/ttyACM1
  ```

Do not use the board LED as proof that UART flashing is progressing. The reliable indicators are ROM `C` output, `uart_uniflash.py` progress, and final `All commands from config file are executed !!!`.

## Power Cycle

Power-cycle means remove board power, wait a moment, then reconnect power. On this setup, USB-C powers the board and Micro-USB provides XDS110/UART.

After a successful UART flash, switch from UART flashing mode back to OSPI boot mode before expecting the flashed app to run:

```text
SW4 BOOTMODE [1:8] = 01000100
```

## LIBUSB_ERROR_ACCESS

Usually fixed by

- reconnecting XDS110
- installing udev rules
- verifying

```bash
ls -l /dev/bus/usb/001/XXX
```

---

# Next Steps

1. Industrial Communications SDK
2. EtherCAT Slave example
3. TMC5160 evaluation
4. Joshua hardware abstraction layer
5. Joshua EtherCAT integration
