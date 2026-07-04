#!/usr/bin/env bash
set -euo pipefail

IFACE="${1:-enp5s0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SLAVEINFO="$ROOT/third_party/SOEM-manual-build/bin/slaveinfo"

if [[ ! -x "$SLAVEINFO" ]]; then
    echo "Missing SOEM slaveinfo binary: $SLAVEINFO"
    echo "Build it first:"
    echo "  $ROOT/scripts/setup_soem_slaveinfo.sh"
    exit 2
fi

exec sudo "$SLAVEINFO" "$IFACE"
