#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
  echo "Usage: $0 <node_name> [args...]" >&2
  exit 1
fi

NODE="$1"; shift || true
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Case 1a: Impl sits next to this script
if [ -x "$SCRIPT_DIR/${NODE}" ]; then
  cd "$SCRIPT_DIR"
  # Export runfiles env vars to help ROS 2 locate resources
  WS_DIR="$(dirname "$SCRIPT_DIR")"
  export RUNFILES_DIR="$WS_DIR"
  export TEST_SRCDIR="$WS_DIR"
  unset RUNFILES_MANIFEST_FILE || true
  unset JAVA_RUNFILES || true
  exec "./${NODE}" "$@"
fi

# Case 1b: Impl sits under ros2/ while this script is at top-level
if [ -x "$SCRIPT_DIR/ros2/${NODE}" ]; then
  cd "$SCRIPT_DIR/ros2"
  WS_DIR="$(dirname "$SCRIPT_DIR")"
  export RUNFILES_DIR="$WS_DIR"
  export TEST_SRCDIR="$WS_DIR"
  unset RUNFILES_MANIFEST_FILE || true
  unset JAVA_RUNFILES || true
  exec "./${NODE}" "$@"
fi

# Case 2: Runfiles layout. Search any *.runfiles under top-level and ros2/ subdir.
for CAND in "$SCRIPT_DIR" "$SCRIPT_DIR/ros2"; do
  for RUNFILES_DIR in "$CAND"/*.runfiles; do
    if [ -d "$RUNFILES_DIR/_main/ros2" ]; then
      cd "$RUNFILES_DIR/_main/ros2"
      WS_DIR="$(dirname "$PWD")"
      export RUNFILES_DIR="$WS_DIR"
      export TEST_SRCDIR="$WS_DIR"
      unset RUNFILES_MANIFEST_FILE || true
      unset JAVA_RUNFILES || true
      exec "./${NODE}" "$@"
    fi
    if [ -d "$RUNFILES_DIR/__main__/ros2" ]; then
      cd "$RUNFILES_DIR/__main__/ros2"
      WS_DIR="$(dirname "$PWD")"
      export RUNFILES_DIR="$WS_DIR"
      export TEST_SRCDIR="$WS_DIR"
      unset RUNFILES_MANIFEST_FILE || true
      unset JAVA_RUNFILES || true
      exec "./${NODE}" "$@"
    fi
  done
done

echo "Could not locate '${NODE}' relative to '$SCRIPT_DIR'." >&2
exit 1

