#!/usr/bin/env bash
# Opt-in SOES artifact; leaves the existing TI-stack artifacts untouched.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
JOSHUA_ROOT="$(cd "$FIRMWARE_DIR/../../.." && pwd)"
SDK_ROOT="${INDUSTRIAL_COMMUNICATIONS_SDK_PATH:-${HOME}/ti/ind_comms_sdk_am243x_09_00_00_03}"
TI_SOC_DIR="$SDK_ROOT/examples/industrial_comms/ethercat_slave_beckhoff_ssc_demo"
TI_PROJECT_DIR="$TI_SOC_DIR/am243x-lp/r5fss0-0_freertos/ti-arm-clang"
for watchdog in JOSHUA_COMM_WATCHDOG_US JOSHUA_TARGET_WATCHDOG_US; do
  value="${!watchdog:-}"
  if [[ ! "$value" =~ ^[1-9][0-9]{0,8}$ ]] || (( value < 10000 )); then
    echo "$watchdog must be explicitly supplied, 10000..999999999 microseconds" >&2; exit 2
  fi
done
# Keep revision/hash identical to MODULE.bazel. Fetch outside the source tree.
SOES_REV=6ef7b9415e936c061ad30626ca00004bdde2ac62
SOES_SHA=da67ea126244b92072f423631cc4c4d6891d1880bb70d065b24acf5230423ac6
BUILD_ROOT="$(mktemp -d)"
trap 'rm -rf "$BUILD_ROOT"' EXIT
mkdir -p "$BUILD_ROOT/build" "$BUILD_ROOT/patched"
curl --fail --location --retry 3 "https://codeload.github.com/OpenEtherCATsociety/SOES/tar.gz/$SOES_REV" -o "$BUILD_ROOT/soes.tar.gz"
echo "$SOES_SHA  $BUILD_ROOT/soes.tar.gz" | sha256sum -c -
tar -xzf "$BUILD_ROOT/soes.tar.gz" -C "$BUILD_ROOT"
SOES_DIR="$BUILD_ROOT/SOES-$SOES_REV"
patch --fuzz=0 -d "$SOES_DIR" -p1 < "$JOSHUA_ROOT/third_party/soes/upload_size.patch"
cp "$TI_SOC_DIR/am243x-lp/tiescsoc.c" "$BUILD_ROOT/patched/"
patch --fuzz=0 "$BUILD_ROOT/patched/tiescsoc.c" < "$FIRMWARE_DIR/patches/soes_soc.patch"
cp "$TI_PROJECT_DIR/linker.cmd" "$BUILD_ROOT/build/"
cp "$TI_SOC_DIR/am243x-lp/r5fss0-0_freertos/example.syscfg" "$BUILD_ROOT/"
cp "$SDK_ROOT/mcu_plus_sdk/tools/boot/xipGen/xipGen.out" "$BUILD_ROOT/xipGen.out"
make -C "$BUILD_ROOT/build" -f "$FIRMWARE_DIR/Makefile.soes" all \
  INDUSTRIAL_COMMUNICATIONS_SDK_PATH="$SDK_ROOT" JOSHUA_ROOT="$JOSHUA_ROOT" \
  JOSHUA_PATCHED_SOURCE_DIR="$BUILD_ROOT/patched" SOES_DIR="$SOES_DIR" \
  JOSHUA_COMM_WATCHDOG_US="$JOSHUA_COMM_WATCHDOG_US" \
  JOSHUA_TARGET_WATCHDOG_US="$JOSHUA_TARGET_WATCHDOG_US" \
  XIPGEN_CMD="$BUILD_ROOT/xipGen.out" "$@"
# Fail closed if a future SDK/template change pulls in the evaluation stack.
if grep -Eq 'EC_API_SLV_|ethercat_slave(\.|_bkhfSsc)' \
    "$BUILD_ROOT/build/ethercat_slave_beckhoff_ssc_demo.release.map"; then
  echo "Unexpected TI slave-stack dependency in SOES link map" >&2; exit 2
fi
mkdir -p "$FIRMWARE_DIR/out"
for suffix in out map appimage appimage.hs_fs; do
  cp "$BUILD_ROOT/build/ethercat_slave_beckhoff_ssc_demo.release.$suffix" \
    "$FIRMWARE_DIR/out/am243_ethercat_jw2_soes.release.$suffix"
done
# Preserve exact dependency source + its license alongside the distributable.
cp "$BUILD_ROOT/soes.tar.gz" "$FIRMWARE_DIR/out/SOES-$SOES_REV.tar.gz"
cp "$SOES_DIR/LICENSE" "$FIRMWARE_DIR/out/SOES-LICENSE"
echo "Built am243_ethercat_jw2_soes (not flashed; hardware qualification pending)."
