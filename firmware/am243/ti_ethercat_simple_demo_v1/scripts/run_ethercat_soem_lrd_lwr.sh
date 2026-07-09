#!/usr/bin/env bash
set -euo pipefail

IFACE="${1:-enp5s0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$ROOT/third_party/SOEM-manual-build/bin/am243_lrd_lwr"

if [[ ! -x "$BIN" ]]; then
    echo "Missing SOEM diagnostic binary: $BIN"
    echo "Build it first:"
    echo "  $ROOT/scripts/setup_soem_slaveinfo.sh"
    exit 2
fi

exec sudo "$BIN" "$IFACE"
