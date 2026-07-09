#!/usr/bin/env bash
set -euo pipefail

IFACE="${1:-enp5s0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SIMPLE_NG="$ROOT/third_party/SOEM-manual-build/bin/simple_ng"

if [[ ! -x "$SIMPLE_NG" ]]; then
    echo "Missing SOEM simple_ng binary: $SIMPLE_NG"
    echo "Build it first:"
    echo "  $ROOT/scripts/setup_soem_slaveinfo.sh"
    exit 2
fi

exec sudo "$SIMPLE_NG" "$IFACE"
