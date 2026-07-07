# AM243 EtherCAT Milestone

## Objective

Bring up a TI EtherCAT slave example on LP-AM243, then use that as the AM243 backend reference for Joshua hardware abstraction work.

AM243 remains one backend implementation. Joshua core interfaces must stay backend-neutral.

## Current State

Completed:

- LP-AM243 board bring-up
- XDS110 debug
- UART console
- MCU+ SDK build
- Hello World debug from CCS
- Hello World flash to OSPI
- Hello World boot from OSPI without CCS

Primary reference:

- TI AM243x Industrial Communications SDK 2026.00.00 documents current EtherCAT SubDevice demos:

  ```text
  examples/industrial_comms/ethercat_subdevice_demo
  ```

  Documentation link:

  ```text
  https://software-dl.ti.com/processor-industrial-sw/esd/ind_comms_sdk/am243x/2026_00_00_06/docs/api_guide_am243x/EXAMPLES_INDUSTRIAL_COMMS_ETHERCAT_SUBDEVICE_DEMOS.html
  ```

  Supported combination:

  ```text
  CPU + OS:   r5fss0-0 freertos
  ICSSG:      ICSSG1
  Toolchain:  ti-arm-clang
  Boards:     am243x-evm, am243x-lp E3 revision
  ```

  The Industrial Communications SDK documentation points back to MCU+ SDK 12.00.00.26 for baseline AM243x setup, which matches the local MCU+ SDK installed on this machine.

Package notes:

- The SDK has `Standard`, `Premium`, and `Premium Limited` variants.
- `Standard` is documented as free on TI.com.
- The example page notes that the SDK EtherCAT examples use an evaluation stack and run for 1 hour.
- The manifest for 2026.00.00 lists Beckhoff EtherCAT SSC Library version 5.13 as a binary in:

  ```text
  source/industrial_comms/ethercat_subdevice/stack/lib/
  ```

Historical reference:

- TI MCU+ SDK 08.00.00.21 documented an EtherCAT slave example:

  ```text
  examples/industrial_protocols/ethercat_slave_beckhoff_ssc_demo
  ```

  Documentation link:

  ```text
  https://software-dl.ti.com/mcu-plus-sdk/esd/AM243X/08_00_00_21/exports/docs/api_guide_am243x/EXAMPLES_INDUSTRIAL_PROTOCOLS_ETHERCAT_SLAVE_BECKHOFF_SSC_DEMO.html
  ```

Current local result:

- Industrial Communications SDK 09.00.00.03 is installed at:

  ```text
  ~/ti/ind_comms_sdk_am243x_09_00_00_03
  ```

- It contains the LP-AM243 Beckhoff SSC EtherCAT example:

  ```text
  examples/industrial_comms/ethercat_slave_beckhoff_ssc_demo/am243x-lp/r5fss0-0_freertos/ti-arm-clang
  ```

- It also contains SDK-shipped EtherCAT slave demos that build without importing external Beckhoff SSC source:

  ```text
  examples/industrial_comms/ethercat_slave_demo/device_profiles/401_simple/am243x-lp/r5fss0-0_freertos/ti-arm-clang
  examples/industrial_comms/ethercat_slave_demo/device_profiles/402_cia/am243x-lp/r5fss0-0_freertos/ti-arm-clang
  examples/industrial_comms/ethercat_slave_demo/device_profiles/ctt/am243x-lp/r5fss0-0_freertos/ti-arm-clang
  ```

- The installed MCU+ SDK 12.00.00.26 does not contain EtherCAT examples.

The 09.00.00.03 release notes require these tool versions:

```text
CCS:            12.4.0
SysConfig:      1.17.0 build 3128
TI ARM CLANG:   2.1.3.LTS
MCU+ SDK:       9.0.0
```

Build attempt with the newer local SysConfig 1.26.3 failed because the 09 SDK SysConfig metadata is incompatible with it. Use SysConfig 1.17.0 for this SDK.

Verified local tool path:

```text
SysConfig:     ~/ti/sysconfig_1.17.0
PRU CGT:       ~/ti/ti-cgt-pru_2.3.3/ti-cgt-pru_2.3.3
ARM Clang:     ~/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS
```

The documented TI ARM Clang 2.1.3.LTS URL in the SDK docs currently resolves to TI's 404 page from this machine. The SDK-shipped `401_simple` EtherCAT slave demo builds successfully with the CCS 21 ARM Clang 5.1.1 compiler already installed locally.

## Known Bumps / Avoid Repeating

- MCU+ SDK 12.00.00.26 does not contain the EtherCAT examples. Use Industrial Communications SDK 09.00.00.03 for the current local bring-up.
- Industrial Communications SDK 09 SysConfig metadata is not compatible with SysConfig 1.26.3. Use:

  ```text
  ~/ti/sysconfig_1.17.0
  ```

- The PRU compiler installer creates a nested tool root on this machine. Use the inner directory:

  ```text
  ~/ti/ti-cgt-pru_2.3.3/ti-cgt-pru_2.3.3
  ```

- The SDK documents TI ARM Clang 2.1.3.LTS, but the documented download URL resolved to TI's 404 page during this bring-up. The SDK-shipped `401_simple` demo has been verified with:

  ```text
  ~/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS
  ```

- Start with `ethercat_slave_demo/device_profiles/401_simple`, not `ethercat_slave_beckhoff_ssc_demo`. The simple demo builds from installed SDK contents.
- The Beckhoff SSC demo needs external Beckhoff/ETG SSC 5i13 source. Without it, the build fails on:

  ```text
  fatal error: 'ecat_def.h' file not found
  ```

- The local flash config for the EtherCAT simple demo intentionally uses the Industrial Communications SDK 09 SBL and application image format (`.appimage.hs_fs`), not the MCU+ SDK 12 Hello World `.mcelf.hs_fs` format.
- The SDK-shipped EtherCAT simple demo uses an evaluation stack. When UART prints:

  ```text
  EVAL VERSION EXPIRED
  ```

  the EtherCAT slave stops responding, and SOEM can no longer connect. This is expected after the evaluation runtime expires. Power-cycle or reset the LP-AM243 to restart the demo timer. For unlimited runtime, rebuild with the licensed Beckhoff SSC flow documented by TI.

## Working Build: SDK EtherCAT Slave Simple Demo

Build:

```bash
scripts/build_ethercat_simple.sh
```

Verified generated image:

```text
~/ti/ind_comms_sdk_am243x_09_00_00_03/examples/industrial_comms/ethercat_slave_demo/device_profiles/401_simple/am243x-lp/r5fss0-0_freertos/ti-arm-clang/ethercat_slave_simple_demo.release.appimage.hs_fs
```

Flash config:

```text
setup/ethercat_simple_sbl_ospi.cfg
```

Flash command:

```bash
scripts/flash_ethercat_simple.sh /dev/ttyACM0
```

Use UART flashing boot mode before flashing, then OSPI boot mode after flashing:

```text
UART flashing: SW4 BOOTMODE [1:8] = 11100000
OSPI boot:     SW4 BOOTMODE [1:8] = 01000100
```

## Beckhoff SSC Demo Status

The Beckhoff SSC demo is not the first bring-up target because it requires external Beckhoff/ETG SSC source files.

Build currently reaches C compilation, then stops at:

```text
../../../tiescappl.c:36:10: fatal error: 'ecat_def.h' file not found
```

TI documents the required flow:

- Obtain Beckhoff EtherCAT Slave Sample Code version 5i13 from ETG/Beckhoff.
- Copy `SlaveFiles.zip` into:

  ```text
  ~/ti/ind_comms_sdk_am243x_09_00_00_03/source/industrial_comms/ethercat_slave/stack/patch/
  ```

- Extract it into the `SlaveFiles` subfolder.
- Build the patched/custom SSC library with the SDK's `makefile.custom-ssc`.

## Next Acceptance Criteria

1. Configure Industrial Communications SDK imports:

   ```bash
   python3 setup/configure_industrial_sdk_09.py
   ```

2. Detect the SDK:

   ```bash
   scripts/check_industrial_sdk.sh
   ```

3. Build the LP-AM243 EtherCAT slave simple demo:

   ```bash
   scripts/build_ethercat_simple.sh
   ```

4. Flash the EtherCAT simple demo to OSPI.
5. Boot the image from OSPI.
6. Observe expected UART output from the EtherCAT example.
7. Connect an EtherCAT master and verify the subdevice enumerates.
8. Identify whether the LP-AM243 board is E3 revision before relying on enhanced link detection or cable redundancy behavior.

## Linux Master Scan: SOEM

The first local master scan uses SOEM because it does not require installing the IgH kernel EtherCAT master service.

Local status:

```text
Ethernet interface: enp5s0
SOEM source:        third_party/SOEM-master
SOEM scan binary:  third_party/SOEM-manual-build/bin/slaveinfo
SOEM cyclic test:  third_party/SOEM-manual-build/bin/simple_ng
SOEM AM243 test:   third_party/SOEM-manual-build/bin/am243_simple_ng
```

`cmake` was not installed on this host during bring-up, so the SOEM samples are built manually with `gcc` using SOEM's default CMake option values.

Build SOEM tools:

```bash
cd firmware/am243/ti_ethercat_simple_demo_v1
scripts/setup_soem_slaveinfo.sh
```

Run scan:

```bash
cd firmware/am243/ti_ethercat_simple_demo_v1
scripts/scan_ethercat_soem.sh enp5s0
```

This command uses `sudo` because SOEM needs raw Ethernet socket access.

Expected result:

```text
ec_init on enp5s0 succeeded.
1 slaves found and configured.
```

Verified scan result:

```text
ecx_init on enp5s0 succeeded.
1 slaves found and configured.
Calculated workcounter 3

Slave:1
 Name:TI EtherCAT Toolkit for AM243X.R5F
 Output size: 64bits
 Input size: 64bits
 State: 4
 Has DC: 1
 Activeports:1.0.0.0
 Man: e000059d ID: 54490025 Rev: 00010000
```

`State: 4` is SAFE-OP during the `slaveinfo` scan. The next milestone is running a cyclic master example that drives the slave to OP and exchanges the 64-bit PDOs.

Run cyclic PDO exchange:

```bash
cd firmware/am243/ti_ethercat_simple_demo_v1
scripts/run_ethercat_soem_am243.sh enp5s0
```

Expected successful startup:

```text
Initializing SOEM on 'enp5s0'... done
Finding autoconfig slaves... 1 slaves found
Overlapped TI ESC mapping of I/O... mapped 8O+8I bytes
Configuring distributed clock... done
Waiting for all slaves in safe operational... done
Setting operational state.. all slaves are now operational
```

The `simple_ng` sample then prints cyclic WKC, output bytes, input bytes, and DC time. Stop with `Ctrl+C` if needed.

Observed bump:

```text
Iteration 5630:  2023 usec  WKC -1 wrong (expected 3)
All slaves resumed OPERATIONAL
```

This means the slave is still OP, but SOEM timed out waiting for process data. The default SOEM `EC_TIMEOUTRET` is 2000 us, and the observed AM243 roundtrip was slightly above that. The local SOEM setup script raises `EC_TIMEOUTRET` to 10000 us for this bring-up.

If WKC remains `-1` even with the longer timeout, do not use stock `simple_ng`. SOEM's mapping code documents that TI ESC requires overlapped I/O mapping when using LRW. Use:

```bash
scripts/run_ethercat_soem_am243.sh enp5s0
```

If LRW process-data frames are sent but not returned, try a split-frame diagnostic that blocks LRW before PDO mapping and uses sequential LRD/LWR process-data frames:

```bash
scripts/run_ethercat_soem_split.sh enp5s0
```

Verified working result:

```text
Setting operational state... all slaves are now operational
Iteration  671:    38 usec  WKC 3  O: 00 00 00 00 00 00 00 00  I: 00 00 00 00 00 00 00 00
```

The matching capture showed one-for-one returned LRD/LWR frames:

```text
Frames by source:
  03:01:01:01:01:01  2323
  01:01:01:01:01:01  2323

Frames by source and command:
  01:01:01:01:01:01  0x0a LRD     680
  03:01:01:01:01:01  0x0a LRD     680
  01:01:01:01:01:01  0x0b LWR     680
  03:01:01:01:01:01  0x0b LWR     680
```

Current SOEM conclusion: for this AM243 simple-demo pairing, cyclic process data works with split LRD/LWR and does not work with LRW, even with overlapped mapping enabled.

Do not spend time retrying LRW for this exact firmware image unless the board-side EtherCAT binary or EEPROM/ESI configuration changes. The `slaveinfo` scan reported `only LRD/LWR:0`, so the slave did not advertise that LRW should be blocked, but packet captures showed the AM243 did not return LRW process-data frames. Treat this as a firmware/configuration mismatch and force split LRD/LWR from the Linux/SOEM master side.

After split LRD/LWR reaches OP with valid WKC, run a PDO walk to write changing output bytes and observe whether the AM243 demo returns changing input bytes:

```bash
scripts/run_ethercat_soem_pdo_walk.sh enp5s0
```

Expected master-side behavior:

```text
WKC 3  O: 00 01 02 03 04 05 06 07
WKC 3  O: 01 02 03 04 05 06 07 08
```

Observed result:

```text
Iteration  436:    45 usec  WKC 3  O: B3 B4 B5 B6 B7 B8 B9 BA  I: B2 00 00 00 00 00 00 00
```

This confirms bidirectional cyclic PDO transport. The shipped simple demo returned changing data in input byte 0, one cycle behind the output seed, while input bytes 1-7 remained zero in this test. Treat this as demo application behavior, not an EtherCAT transport failure.

If the slave reaches OP but cyclic WKC remains `-1`, check host NIC behavior:

```bash
scripts/prepare_ethercat_nic.sh enp5s0
scripts/run_ethercat_soem_am243.sh enp5s0
```

Capture EtherCAT frames during a short run:

```bash
# terminal 1
scripts/capture_ethercat.sh enp5s0 5

# terminal 2, while capture is running
scripts/run_ethercat_soem_am243.sh enp5s0
```

The capture script writes a timestamped file under `/tmp` by default. If an explicit path already exists, the script automatically adds a timestamp suffix instead of overwriting it. This avoids a common `tcpdump: Permission denied` bump where the previous pcap was created by `tcpdump` as `nobody:nogroup`.

Read the capture summary:

```bash
tcpdump -nn -e -r /tmp/am243_ethercat_capture_YYYYMMDD_HHMMSS.pcap
```

Summarize EtherCAT commands in the capture:

```bash
scripts/analyze_ethercat_capture.sh /tmp/am243_ethercat_capture_YYYYMMDD_HHMMSS.pcap
```

Observed from the first cyclic-failure capture:

```text
Frames by source:
  03:01:01:01:01:01  1317
  01:01:01:01:01:01  1665

Frames by source and command:
  01:01:01:01:01:01  0x0c LRW     348
```

There were no returned LRW `0x0c` frames from `03:01:01:01:01:01` in that capture. This matches SOEM reporting cyclic `WKC -1`: the master sends LRW process-data frames, but the AM243 response is not visible to the host capture.

If no slaves are found:

- Confirm the AM243 EtherCAT app is still running on UART.
- Confirm the cable is connected directly from the PC NIC to the AM243 EtherCAT IN/ETH0/Port 0 side.
- Avoid a normal Ethernet switch.
- Try the other AM243 RJ45 if the board silkscreen does not clearly identify IN/OUT.

Reference documentation roots:

```text
file: ~/ti/ind_comms_sdk_am243x_09_00_00_03/docs/api_guide_am243x/index.html
https://software-dl.ti.com/processor-industrial-sw/esd/ind_comms_sdk/am243x/2026_00_00_06/docs/api_guide_am243x/index.html
```

Note: the SDK examples use an evaluation stack and run for 1 hour. For unlimited runtime, rebuild the Beckhoff SSC library using the SDK's documented patch flow.

Expected example UART output begins with:

```text
EtherCAT Device
EtherCAT Internal application
Revision/Type
Firmware Version
SYNC0 task started
SYNC1 task started
```

## Board Mode

For command-line flash using UART Uniflash:

```text
SW4 BOOTMODE [1:8] = 11100000
```

For booting the flashed image from OSPI:

```text
SW4 BOOTMODE [1:8] = 01000100
```

## Joshua Direction

Do not expose AM243-specific concepts in Joshua core APIs.

Expected backend shape:

```text
Joshua
  core
  common hardware interfaces
  hardware backends
    am243_ethercat
    linux_ethercat
    stm32
    teensy
    unitree
    simulation
```

The AM243 EtherCAT example should be treated as a hardware-specific transport and timing backend, not as the architecture for Joshua.
