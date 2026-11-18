#!/bin/sh
# Host Python is Ubuntu's preinstalled stdlib runtime; all project deps stay in Docker.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$SCRIPT_DIR/mcp_runtime.py" "$@"
