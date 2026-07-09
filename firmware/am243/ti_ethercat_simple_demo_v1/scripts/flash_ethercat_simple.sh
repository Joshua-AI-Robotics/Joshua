#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 /dev/ttyACM0"
    exit 1
fi

PORT="$1"
SDK="${INDUSTRIAL_COMMUNICATIONS_SDK_PATH:-$HOME/ti/ind_comms_sdk_am243x_09_00_00_03}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CFG_TEMPLATE="$ROOT/setup/ethercat_simple_sbl_ospi.cfg"
CFG="$(mktemp)"
trap 'rm -f "$CFG"' EXIT

sed "s|@INDUSTRIAL_COMMUNICATIONS_SDK_PATH@|$SDK|g" "$CFG_TEMPLATE" > "$CFG"

VENV="$HOME/myWorkspace/venvs/ti"
if [[ ! -d "$VENV" ]]; then
    python3 -m venv "$VENV"
fi
# shellcheck disable=SC1090
source "$VENV/bin/activate"

python3 -m pip install --quiet -r "$ROOT/setup/requirements.txt"

python3 "$SDK/mcu_plus_sdk/tools/boot/uart_uniflash.py" \
    -p "$PORT" \
    --cfg "$CFG"
