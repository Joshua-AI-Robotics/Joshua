#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
JOSHUA_ROOT="$(cd "$FIRMWARE_DIR/../../.." && pwd)"
JOSHUA_WIRE_VERSION="${JOSHUA_WIRE_VERSION:-1}"
JOSHUA_ETHERCAT_PROFILE="${JOSHUA_ETHERCAT_PROFILE:-ti-demo}"
case "$JOSHUA_WIRE_VERSION" in
  1|2) ;;
  *) echo "JOSHUA_WIRE_VERSION must be 1 or 2" >&2; exit 2 ;;
esac
case "$JOSHUA_ETHERCAT_PROFILE" in
  ti-demo) ARTIFACT_NAME="am243_dual_transport_v${JOSHUA_WIRE_VERSION}" ;;
  jw2)
    if [[ "$JOSHUA_WIRE_VERSION" != 2 ]]; then
      echo "JW2 EtherCAT requires JOSHUA_WIRE_VERSION=2" >&2; exit 2
    fi
    for watchdog in JOSHUA_COMM_WATCHDOG_US JOSHUA_TARGET_WATCHDOG_US; do
      value="${!watchdog:-}"
      if [[ ! "$value" =~ ^[1-9][0-9]{0,8}$ ]] || (( value < 10000 )); then
        echo "$watchdog must be explicitly supplied, 10000..999999999 microseconds" >&2; exit 2
      fi
    done
    ARTIFACT_NAME="am243_ethercat_jw2"
    ;;
  *) echo "JOSHUA_ETHERCAT_PROFILE must be ti-demo or jw2" >&2; exit 2 ;;
esac
SDK_ROOT="${INDUSTRIAL_COMMUNICATIONS_SDK_PATH:-${HOME}/ti/ind_comms_sdk_am243x_09_00_00_03}"
TI_PROFILE_DIR="$SDK_ROOT/examples/industrial_comms/ethercat_slave_demo/device_profiles/401_simple"
TI_PROJECT_DIR="$TI_PROFILE_DIR/am243x-lp/r5fss0-0_freertos/ti-arm-clang"

for required in "$SDK_ROOT" "$TI_PROJECT_DIR/makefile" "$TI_PROJECT_DIR/linker.cmd" \
  "$TI_PROFILE_DIR/EtherCAT_Slave_Simple.c" "$TI_PROFILE_DIR/am243x-lp/r5fss0-0_freertos/example.syscfg"; do
  if [[ ! -e "$required" ]]; then
    echo "Missing required TI SDK input: $required" >&2
    exit 2
  fi
done

BUILD_ROOT="$(mktemp -d)"
trap 'rm -rf "$BUILD_ROOT"' EXIT
mkdir -p "$BUILD_ROOT/build" "$BUILD_ROOT/patched"
cp "$TI_PROJECT_DIR/linker.cmd" "$BUILD_ROOT/build/linker.cmd"
cp "$TI_PROFILE_DIR/am243x-lp/r5fss0-0_freertos/example.syscfg" "$BUILD_ROOT/example.syscfg"
cp "$TI_PROFILE_DIR/EtherCAT_Slave_Simple.c" "$BUILD_ROOT/patched/EtherCAT_Slave_Simple.c"
cp "$SDK_ROOT/mcu_plus_sdk/tools/boot/xipGen/xipGen.out" "$BUILD_ROOT/xipGen.out"
if [[ "$JOSHUA_ETHERCAT_PROFILE" == jw2 ]]; then
  cp "$TI_PROFILE_DIR/ecSlvSimple.c" "$BUILD_ROOT/patched/ecSlvSimple.c"
  patch --fuzz=0 "$BUILD_ROOT/patched/ecSlvSimple.c" < "$FIRMWARE_DIR/patches/jwec_profile.patch"
else
  patch "$BUILD_ROOT/patched/EtherCAT_Slave_Simple.c" < "$FIRMWARE_DIR/patches/dual_transport.patch"
fi

make -C "$BUILD_ROOT/build" -f "$FIRMWARE_DIR/Makefile" all \
  INDUSTRIAL_COMMUNICATIONS_SDK_PATH="$SDK_ROOT" \
  TI_PROJECT_DIR="$TI_PROJECT_DIR" \
  JOSHUA_ROOT="$JOSHUA_ROOT" \
  JOSHUA_WIRE_VERSION="$JOSHUA_WIRE_VERSION" \
  JOSHUA_ETHERCAT_PROFILE="$JOSHUA_ETHERCAT_PROFILE" \
  JOSHUA_COMM_WATCHDOG_US="${JOSHUA_COMM_WATCHDOG_US:-0}" \
  JOSHUA_TARGET_WATCHDOG_US="${JOSHUA_TARGET_WATCHDOG_US:-0}" \
  JOSHUA_PATCHED_SOURCE_DIR="$BUILD_ROOT/patched" \
  XIPGEN_CMD="$BUILD_ROOT/xipGen.out" "$@"

mkdir -p "$FIRMWARE_DIR/out"
cp "$BUILD_ROOT/build/ethercat_slave_simple_demo.release.out" \
  "$FIRMWARE_DIR/out/${ARTIFACT_NAME}.release.out"
cp "$BUILD_ROOT/build/ethercat_slave_simple_demo.release.appimage" \
  "$FIRMWARE_DIR/out/${ARTIFACT_NAME}.release.appimage"
cp "$BUILD_ROOT/build/ethercat_slave_simple_demo.release.appimage.hs_fs" \
  "$FIRMWARE_DIR/out/${ARTIFACT_NAME}.release.appimage.hs_fs"

echo "Built AM243 firmware ($JOSHUA_ETHERCAT_PROFILE):"
ls -lh "$FIRMWARE_DIR"/out/"$ARTIFACT_NAME".release.*
