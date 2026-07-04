#!/usr/bin/env bash
set -euo pipefail

IFACE="${1:-enp5s0}"
DURATION="${2:-5}"
OUT="${3:-/tmp/am243_ethercat_capture_$(date +%Y%m%d_%H%M%S).pcap}"

if [[ -e "$OUT" ]]; then
    DIR="$(dirname "$OUT")"
    FILE="$(basename "$OUT")"
    STEM="${FILE%.*}"
    EXT="${FILE##*.}"

    if [[ "$STEM" == "$EXT" ]]; then
        OUT="$DIR/${FILE}_$(date +%Y%m%d_%H%M%S)"
    else
        OUT="$DIR/${STEM}_$(date +%Y%m%d_%H%M%S).${EXT}"
    fi

    echo "Output file already exists; using $OUT"
fi

echo "Capturing EtherCAT frames on $IFACE for ${DURATION}s -> $OUT"
exec sudo timeout "$DURATION" tcpdump -i "$IFACE" -s 0 -w "$OUT" 'ether proto 0x88a4'
