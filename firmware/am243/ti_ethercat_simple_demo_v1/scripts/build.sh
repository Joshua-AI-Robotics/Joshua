#!/usr/bin/env bash
set -e

SDK="$HOME/ti/mcu_plus_sdk_am243x_12_00_00_26"
HELLO="$SDK/examples/hello_world/am243x-lp/r5fss0-0_nortos/ti-arm-clang"

make -C "$HELLO" clean
make -C "$HELLO" all

echo "Build complete: $HELLO"
