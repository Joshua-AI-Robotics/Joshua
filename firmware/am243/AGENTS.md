# LP-AM243 board setup and flashing

Follow [firmware/AGENTS.md](../AGENTS.md), including its shared workflow and
required board-guide format, and the root hardware authorization rules.
This is the authoritative physical-board procedure. Child guides own variant
implementation/build details and dated evidence, not another flashing procedure.

## Identity and status

These configurations target TI LP-AM243 LaunchPad **HS-FS** and OSPI boot.
Do not reuse them for another board, security type, or memory layout. The JW
artifacts currently expose a software-only actuator channel; they do not drive
STEP/DIR GPIO. Native tests and earlier stationary bench records do not establish
physical motor safety or EtherCAT timing/endurance qualification.

## Firmware selection

| Selected profile | Build profile | Application under `joshua_dual_transport/out/` | Capability |
| --- | --- | --- | --- |
| `uart` | `ti-demo` | `am243_dual_transport_rev4.release.appimage.hs_fs` | JW UART; EtherCAT remains TI echo demo |
| `jw` | `jw` | `am243_ethercat_jw.release.appimage.hs_fs` | JW EtherCAT over TI evaluation stack; no UART protocol task |
| `jw-soes` | `jw-soes` | `am243_ethercat_jw_soes.release.appimage.hs_fs` | SOES JW EtherCAT candidate; no TI evaluation slave stack |

The TI-stack artifacts retain their evaluation limits. SOES requires separate
qualification before replacing them. The [legacy vendor guide](ti_ethercat_simple_demo_v1/AGENTS.md)
provides Hello World/TI demo artifacts; neither is a JoshuaWire runtime image.

## Prerequisites and install

Use the shared Docker development environment with externally installed TI
inputs mounted at consistent paths. A TI account/vendor downloads are required;
this repository does not distribute the SDK. The recorded toolchain combination:

| Input | Required reference |
| --- | --- |
| Industrial Communications SDK | `09.00.00.03` for all JW images and the vendor EtherCAT demo |
| CCS / ARM Clang | `21.0.0.00014` / `ti-cgt-armllvm_5.1.1.LTS` |
| PRU compiler | `2.3.3` |
| SysConfig | `1.17.0` for Industrial SDK 09 |
| MCU+ SDK / SysConfig for legacy Hello World only | `12.00.00.26` / `1.26.3` |

Set `INDUSTRIAL_COMMUNICATIONS_SDK_PATH` to the mounted SDK 09 root. Make imports
must point at the mounted CCS, compiler, PRU, and SysConfig installations. The
existing setup helper can show changes before editing that external SDK:

```bash
python3 firmware/am243/ti_ethercat_simple_demo_v1/setup/configure_industrial_sdk_09.py \
  --sdk-root "$INDUSTRIAL_COMMUNICATIONS_SDK_PATH" --dry-run
```

Supply its tool-path overrides for your installation; inspect resulting compiler
paths before applying changes without `--dry-run`. Install the SDK flash utility's
Python dependencies in the container or its selected venv:

```bash
python3 -m pip install -r firmware/am243/ti_ethercat_simple_demo_v1/setup/requirements.txt
```

SDK/toolchains are external and absent from CI; configuration preparation is
tested with fake SDK files. That does not verify a real toolchain installation.

## Build

Use repository-root commands from the implementation guide:
[UART](joshua_dual_transport/AGENTS.md#build),
[TI JW EtherCAT](joshua_dual_transport/AGENTS.md#opt-in-jw-ethercat-profile), and
[SOES](joshua_dual_transport/AGENTS.md#opt-in-soes-replacement).
EtherCAT builds require deliberately supplied watchdog intervals; example values
are software-test inputs, not qualified physical limits. Record the selected
profile and artifact hash before proceeding. A build never flashes hardware.

## Wiring / Pinout

Use the LP-AM243 power USB-C and XDS110 Micro-USB data/debug connections. The
selected UART is 115200 8-N-1 for the JW runtime; identify the actual XDS110 port
rather than assuming a tty number. EtherCAT uses the LP-AM243 IN/OUT ports and
requires the intended host NIC and reviewed link wiring. Current JW images
retain STEP/DIR configuration but do not drive physical STEP/DIR outputs.

## Preparation

Review LP-AM243 revision/security type, image profile, bootloader, power/wiring,
and the intended XDS110 UART. Close terminals and other serial owners. Keep motor
power disconnected for initial upload/protocol checks. Set SW4 with power off:

```text
UART flashing: SW4 BOOTMODE [1:8] = 11100000
OSPI boot:     SW4 BOOTMODE [1:8] = 01000100
```

Power-cycle into UART mode. The ROM prints `C` periodically on the selected UART;
confirm its USB identity and close the terminal before upload. Use the actual
stable device path, not a copied tty number.

## Flash

First prepare a new configuration without opening a device. Replace the port
placeholder and select exactly one profile from the table:

```bash
JOSHUA_AM243_PROFILE=uart
JOSHUA_PORT=/dev/serial/by-id/REPLACE_WITH_VERIFIED_AM243_UART
JOSHUA_FLASH_CFG=/tmp/joshua-am243-flash.cfg
python3 firmware/am243/prepare_flash.py \
  --sdk "$INDUSTRIAL_COMMUNICATIONS_SDK_PATH" \
  --profile "$JOSHUA_AM243_PROFILE" --port "$JOSHUA_PORT" \
  --operation flash --output "$JOSHUA_FLASH_CFG"
```

The helper verifies nonempty SDK/input files, selects the exact profile artifact,
prints its SHA256 and the command to review, and refuses to overwrite a config.
It does not validate the image's contents or security type and never invokes the
SDK flash utility. Inputs must have whitespace-free paths supported by the cfg
format. Preserve the reviewed image/hash until the operation finishes.

Its layout matches the checked-in SDK 09 LP-AM243 HS-FS template: UART flash writer,
OSPI PHY tuning, OSPI SBL at `0x0`, and the selected application at `0x80000`.
Inspect both the bootloader and application paths before executing the separate,
currently authorized hardware operation:

```bash
python3 "$INDUSTRIAL_COMMUNICATIONS_SDK_PATH/mcu_plus_sdk/tools/boot/uart_uniflash.py" \
  -p "$JOSHUA_PORT" --cfg "$JOSHUA_FLASH_CFG"
```

Require successful completion and retain the full log. To verify the stored SBL
and application, prepare a **different new file** with `--operation flashverify`
and execute its printed command separately while still in UART mode. Verification
configuration omits PHY programming. The helper's tests establish selection and
configuration behavior; this cleanup has not flashed or flash-verified a board.

## Return to normal boot

Power off, restore SW4 to OSPI boot, and power-cycle. Re-identify the console/UART
and observe application startup. For a failed boot, return deliberately to UART
mode and inspect the selected image, HS-FS type, SBL, offsets, tool versions, and
flash log before retrying. Never substitute the vendor demo application for JW.

## Verify

For UART, use the [JW serial preflight and handshake](../../docs/JOSHUA_WIRE_VALIDATION.md).
Run its dry-run before any confirmed hardware access; default handshake resets,
identifies, and resets without configuring or enabling an actuator.
For EtherCAT, use the selected profile's descriptor/mapping and
[host qualification limits](../../robot/comm/ethercat/AGENTS.md).
A bus scan/OP transition accesses real hardware and requires its own reviewed setup;
it is not part of this preparation helper or automated tests.

## Recovery, evidence, and limitations

Record commit, board/security revision, SDK/toolchain versions, profile, artifact
hash, actual UART identity, SW4 settings, bootloader/application verification,
commands/logs, power state, and remaining qualification gaps. Keep dated stationary
bench results in the implementation guide; do not promote them to motion/timing
qualification. The TI evaluation limit, SOES qualification, and lack of a physical
motor backend remain explicitly documented.

Legacy vendor flashing uses its own MCU+ 12 Hello World or Industrial SDK 09
EtherCAT config; see the [legacy guide](ti_ethercat_simple_demo_v1/AGENTS.md#flash).
Those scripts select vendor applications, so they must never upload a JW artifact
by accident. Their historical examples are not current Joshua runtime verification.

## Directory navigation

- [JoshuaWire implementation and profiles](joshua_dual_transport/AGENTS.md)
- [Legacy TI vendor artifacts](ti_ethercat_simple_demo_v1/AGENTS.md)
