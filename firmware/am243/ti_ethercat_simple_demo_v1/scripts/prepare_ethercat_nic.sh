#!/usr/bin/env bash
set -euo pipefail

IFACE="${1:-enp5s0}"

echo "Disabling common offloads on $IFACE for EtherCAT raw-frame testing"

features=(
    rx
    tx
    sg
    tso
    gso
    gro
    rxvlan
    txvlan
)

for feature in "${features[@]}"; do
    if sudo ethtool -K "$IFACE" "$feature" off 2>/dev/null; then
        echo "  $feature off"
    else
        echo "  $feature unchanged"
    fi
done

echo
sudo ethtool -k "$IFACE" | sed -n '1,28p'
