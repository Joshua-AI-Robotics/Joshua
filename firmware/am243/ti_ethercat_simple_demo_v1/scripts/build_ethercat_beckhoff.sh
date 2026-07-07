#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK="${INDUSTRIAL_COMMUNICATIONS_SDK_PATH:-$HOME/ti/ind_comms_sdk_am243x_09_00_00_03}"
EXAMPLE="$SDK/examples/industrial_comms/ethercat_slave_beckhoff_ssc_demo/am243x-lp/r5fss0-0_freertos/ti-arm-clang"

CCS_PATH="${CCS_PATH:-$HOME/ti/ccs1240/ccs}"
SYSCFG_PATH="${SYSCFG_PATH:-$HOME/ti/sysconfig_1.17.0}"
ARM_CLANG_PATH="${CGT_TI_ARM_CLANG_PATH:-$HOME/ti/ti-cgt-armllvm_2.1.3.LTS}"
PRU_CGT_PATH="${CGT_TI_PRU_PATH:-$HOME/ti/ti-cgt-pru_2.3.3/ti-cgt-pru_2.3.3}"

missing=0
for path in "$SDK" "$EXAMPLE" "$SYSCFG_PATH" "$ARM_CLANG_PATH" "$PRU_CGT_PATH"; do
    if [[ ! -e "$path" ]]; then
        echo "Missing required path: $path"
        missing=1
    fi
done

if [[ "$missing" -ne 0 ]]; then
    echo "Install the Industrial Communications SDK 09 toolchain listed in docs/ethercat.md."
    exit 2
fi

python3 "$ROOT/setup/configure_industrial_sdk_09.py" \
    --sdk-root "$SDK" \
    --ccs-dir "${CCS_PATH#"$HOME/ti/"}" \
    --sysconfig-dir "${SYSCFG_PATH#"$HOME/ti/"}" \
    --compiler-dir "${ARM_CLANG_PATH#"$HOME/ti/"}" \
    --pru-compiler-dir "${PRU_CGT_PATH#"$HOME/ti/"}"

make -C "$EXAMPLE" all \
    CCS_PATH="$CCS_PATH" \
    SYSCFG_PATH="$SYSCFG_PATH" \
    CGT_TI_ARM_CLANG_PATH="$ARM_CLANG_PATH" \
    CGT_TI_PRU_PATH="$PRU_CGT_PATH"
