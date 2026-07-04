#!/usr/bin/env bash
set -e

CCS="$HOME/ti/ccs2100/ccs/theia/ccstudio"
if [[ ! -x "$CCS" ]]; then
    echo "CCS executable not found: $CCS"
    exit 1
fi

"$CCS" "$@"
