#!/usr/bin/env bash
set -euo pipefail

SEARCH_DIRS=(
    "$HOME/Downloads"
    "/tmp"
    "$HOME/ti"
)

echo "Searching for AM243x Industrial Communications SDK artifacts..."

FOUND=0
for dir in "${SEARCH_DIRS[@]}"; do
    [[ -d "$dir" ]] || continue
    echo
    echo "Checking: $dir"
    while IFS= read -r path; do
        [[ -n "$path" ]] || continue
        echo "  $path"
        FOUND=1
    done < <(
        find "$dir" -maxdepth 3 \( \
            -iname '*ind*comm*am243x*' -o \
            -iname '*industrial*comm*am243x*' -o \
            -path '*/examples/industrial_comms/ethercat_subdevice_demo' \
        \) 2>/dev/null | sort
    )
done

if [[ "$FOUND" -eq 0 ]]; then
    echo
    echo "No Industrial Communications SDK artifacts found."
    echo "Download/install AM243x Industrial Communications SDK 2026.00.00, then rerun:"
    echo "  scripts/find_industrial_sdk_artifact.sh"
    exit 2
fi

echo
echo "Artifact search complete."
