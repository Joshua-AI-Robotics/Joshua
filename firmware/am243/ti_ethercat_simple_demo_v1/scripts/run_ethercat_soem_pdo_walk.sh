#!/usr/bin/env bash
set -euo pipefail

IFACE="${1:-enp5s0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
AM243_PDO_WALK="$ROOT/third_party/SOEM-manual-build/bin/am243_pdo_walk_ng"

if [[ ! -x "$AM243_PDO_WALK" ]]; then
    echo "Missing SOEM PDO walk binary: $AM243_PDO_WALK"
    echo "Build it first:"
    echo "  $ROOT/scripts/setup_soem_slaveinfo.sh"
    exit 2
fi

exec sudo "$AM243_PDO_WALK" "$IFACE"
