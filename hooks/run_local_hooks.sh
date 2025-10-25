#!/usr/bin/env bash
# This hook is used to run local custom hooks.

set -euo pipefail

ROOT_DIR="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
HOOKS_DIR="$ROOT_DIR/hooks"

if [ ! -d "$HOOKS_DIR" ]; then
  # Nothing to run
  exit 0
fi

exit_code=0
# Exclude the runner itself and the sample template
EXCLUDE_BASENAMES=("run_local_hooks.sh" "sample_check.py")

should_skip() {
  local base="$1"
  for ex in "${EXCLUDE_BASENAMES[@]}"; do
    if [[ "$base" == "$ex" ]]; then
      return 0
    fi
  done
  return 1
}

for script in "$HOOKS_DIR"/*.sh "$HOOKS_DIR"/*.py; do
  [ -e "$script" ] || continue
  base_name="$(basename "$script")"
  if should_skip "$base_name"; then
    continue
  fi
  echo "Running local hook: $script"
  if [[ "$script" == *.py ]]; then
    python3 "$script" || exit_code=$?
  else
    bash "$script" || exit_code=$?
  fi
done

exit $exit_code
