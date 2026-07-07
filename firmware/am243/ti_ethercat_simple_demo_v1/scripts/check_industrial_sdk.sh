#!/usr/bin/env bash
set -euo pipefail

SEARCH_ROOT="${1:-$HOME/ti}"

if [[ ! -d "$SEARCH_ROOT" ]]; then
    echo "Search root not found: $SEARCH_ROOT"
    exit 1
fi

echo "Searching for EtherCAT/Industrial Communications SDK content under: $SEARCH_ROOT"

mapfile -t SDK_CANDIDATES < <(
    find "$SEARCH_ROOT" -maxdepth 2 -type d \( \
        -iname 'mcu_plus_sdk_am243x*' -o \
        -iname '*industrial*comm*' -o \
        -iname '*ind*comm*' -o \
        -iname '*ind_comms*' \
    \) | sort
)

if [[ ${#SDK_CANDIDATES[@]} -eq 0 ]]; then
    echo "No MCU+ SDK or Industrial Communications SDK candidates found."
    echo "Install the required SDK under ~/ti, then rerun:"
    echo "  scripts/check_industrial_sdk.sh"
    exit 2
fi

echo "Candidate SDK directories:"
printf '  %s\n' "${SDK_CANDIDATES[@]}"

FOUND_ETHERCAT=0
for sdk in "${SDK_CANDIDATES[@]}"; do
    echo
    echo "Checking: $sdk"
    mapfile -t ETHERCAT_PATHS < <(
        find "$sdk" -maxdepth 8 \( \
            -path '*/examples/industrial_comms*' -o \
            -path '*/examples/industrial_protocols*' -o \
            -iname '*ethercat*' -o \
            -iname '*tiesc*' -o \
            -iname '*beckhoff*' \
        \) | sort
    )
    if [[ ${#ETHERCAT_PATHS[@]} -gt 0 ]]; then
        printf '  %s\n' "${ETHERCAT_PATHS[@]:0:40}"
        FOUND_ETHERCAT=1
    else
        echo "  No EtherCAT/TIESC paths found."
    fi
done

if [[ "$FOUND_ETHERCAT" -eq 0 ]]; then
    echo
    echo "No EtherCAT/TIESC/Beckhoff paths found in detected SDK candidates."
    echo "Known current Industrial Communications SDK 2026.00.00 example path:"
    echo "  examples/industrial_comms/ethercat_subdevice_demo"
    echo "Known historical MCU+ SDK 08.00.00.21 example path:"
    echo "  examples/industrial_protocols/ethercat_slave_beckhoff_ssc_demo"
    exit 3
fi

echo
echo "EtherCAT SDK content check complete."
