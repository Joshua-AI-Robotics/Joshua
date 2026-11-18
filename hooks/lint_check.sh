#!/usr/bin/env bash
set -euo pipefail

checker="$(dirname "$0")/lint_check.py"
if [[ -f /.dockerenv || "${JOSHUA_LINT_CONTAINER:-}" == 1 ]]; then
  # Keep the lint venv out of the robot runtime's Python environment.
  if [[ -x /opt/joshua-lint/bin/python3 ]]; then
    export PATH="/opt/joshua-lint/bin:$PATH"
  fi
  exec python3 "$checker" "$@"
fi

# Discover files with host Git before entering Docker, including linked worktrees.
exec python3 "$checker" --docker "$@"
