#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <capture.pcap>" >&2
    exit 2
fi

PCAP="$1"

if [[ ! -r "$PCAP" ]]; then
    echo "Capture file is not readable: $PCAP" >&2
    exit 1
fi

tcpdump -nn -e -xx -r "$PCAP" 2>/dev/null | awk '
function cmd_name(cmd) {
    if (cmd == "01") return "APRD"
    if (cmd == "02") return "APWR"
    if (cmd == "04") return "FPRD"
    if (cmd == "05") return "FPWR"
    if (cmd == "07") return "BRD"
    if (cmd == "08") return "BWR"
    if (cmd == "0a") return "LRD"
    if (cmd == "0b") return "LWR"
    if (cmd == "0c") return "LRW"
    if (cmd == "0d") return "ARMW"
    if (cmd == "0e") return "FRMW"
    return "UNKNOWN"
}

/^[0-9][0-9]:/ {
    src = $2
    dst = $4
    sub(/,/, "", dst)
    next
}

/^[[:space:]]*0x0010:/ && src != "" {
    cmd = substr($2, 1, 2)
    idx = substr($2, 3, 2)
    key = src " " cmd
    count[key]++
    by_src[src]++
    by_cmd[cmd]++
    idx_key = src " " cmd " " idx
    idx_count[idx_key]++
}

END {
    print "EtherCAT command summary"
    print ""
    print "Frames by source:"
    for (s in by_src) {
        printf "  %s  %d\n", s, by_src[s]
    }

    print ""
    print "Frames by command:"
    for (c in by_cmd) {
        printf "  0x%s %-7s %d\n", c, cmd_name(c), by_cmd[c]
    }

    print ""
    print "Frames by source and command:"
    for (k in count) {
        split(k, p, " ")
        printf "  %s  0x%s %-7s %d\n", p[1], p[2], cmd_name(p[2]), count[k]
    }

    print ""
    print "Interpretation:"
    print "  PC source 01:01:01:01:01:01 sends master frames."
    print "  AM243 source 03:01:01:01:01:01 indicates returned frames."
    print "  For cyclic SOEM process data, LRW 0x0c should appear from both sources."
}
'
