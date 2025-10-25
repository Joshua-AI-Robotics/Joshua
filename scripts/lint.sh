#!/usr/bin/env bash
set -euo pipefail

# Ensure ~/.local/bin is available (common location for --user installs)
if ! command -v pre-commit >/dev/null 2>&1; then
  if [ -x "$HOME/.local/bin/pre-commit" ]; then
    export PATH="$HOME/.local/bin:$PATH"
  fi
fi

if command -v pre-commit >/dev/null 2>&1; then
  PRECOMMIT="pre-commit"
elif python3 -c "import pre_commit" >/dev/null 2>&1; then
  PRECOMMIT="python3 -m pre_commit"
else
  echo "pre-commit not found. Install it with: python3 -m pip install --user pre-commit" >&2
  exit 1
fi

# Helper to run a specific hook and collect failures without stopping early
fail=0
run_hook() {
  if ! "$PRECOMMIT" run "$1" --all-files "${@:2}"; then
    fail=1
  fi
}

# Auto-fix selected hooks only (on demand when you run this script)
run_hook trailing-whitespace --hook-stage manual
run_hook end-of-file-fixer --hook-stage manual
run_hook check-yaml --hook-stage manual --allow-multiple-documents
run_hook clang-format --hook-stage manual

# Check-only for pre-push: validate formatting when pushing (no auto-fix here)
run_hook clang-format --hook-stage pre-push

if [ "$fail" -ne 0 ]; then
  echo "One or more hooks modified files or failed. Stage fixes and rerun."
  exit 1
fi

echo "Linting complete."
