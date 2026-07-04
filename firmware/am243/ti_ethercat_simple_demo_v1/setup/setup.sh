#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK="${MCU_PLUS_SDK_PATH:-$HOME/ti/mcu_plus_sdk_am243x_12_00_00_26}"
HELLO="$SDK/examples/hello_world/am243x-lp/r5fss0-0_nortos/ti-arm-clang"
VENV="$HOME/myWorkspace/venvs/ti"

if [[ ! -d "$VENV" ]]; then
    python3 -m venv "$VENV"
fi
# shellcheck disable=SC1090
source "$VENV/bin/activate"

python3 "$ROOT/setup/configure_sdk.py"
python3 -m pip install --quiet -r "$ROOT/setup/requirements.txt"

python3 - <<'PY'
import elftools, construct
print("Python deps OK: pyelftools, construct")
PY

make -C "$HELLO" clean
make -C "$HELLO" all

echo "AM243 Hello World build OK"
