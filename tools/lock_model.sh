#!/usr/bin/env bash
# Regenerate per-model requirements.lock files.
#
# Usage: tools/lock_model.sh <model_name>
# Example: tools/lock_model.sh smolvla
set -euo pipefail

MODEL="${1:?usage: lock_model.sh <model_name>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/ai/models/$MODEL"
IN="$DIR/requirements.in"

if [[ ! -f "$IN" ]]; then
  echo "Missing $IN" >&2
  exit 1
fi

pip-compile --allow-unsafe --output-file="$DIR/requirements.lock" "$IN"

if command -v python3.12 >/dev/null 2>&1; then
  python3.12 -m piptools compile --allow-unsafe \
    --output-file="$DIR/requirements_3.12.lock" "$IN"
else
  echo "python3.12 not found; skipping requirements_3.12.lock" >&2
fi

echo "Wrote $DIR/requirements.lock and $DIR/requirements_3.12.lock"
