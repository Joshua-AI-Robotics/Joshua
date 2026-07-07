#!/usr/bin/env bash
set -e

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 /dev/ttyACM0"
    exit 1
fi

PORT="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK="${MCU_PLUS_SDK_PATH:-$HOME/ti/mcu_plus_sdk_am243x_12_00_00_26}"
CFG_TEMPLATE="$ROOT/setup/hello_world_sbl_ospi.cfg"
CFG="$(mktemp)"
trap 'rm -f "$CFG"' EXIT

sed "s|@MCU_PLUS_SDK_PATH@|$SDK|g" "$CFG_TEMPLATE" > "$CFG"

VENV="$HOME/myWorkspace/venvs/ti"
if [[ ! -d "$VENV" ]]; then
    python3 -m venv "$VENV"
fi
# shellcheck disable=SC1090
source "$VENV/bin/activate"

python3 "$ROOT/setup/configure_sdk.py"
python3 -m pip install --quiet -r "$ROOT/setup/requirements.txt"

python3 "$SDK/tools/boot/uart_uniflash.py" \
    -p "$PORT" \
    --cfg "$CFG"
