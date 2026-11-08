#!/usr/bin/env bash
set -euo pipefail

root=/workspace/ai/train/locomotion
out=/workspace/output/spider_rl
env_python="$out/venv/bin/python"
mkdir -p "$out/home" "$out/cache"

case "${1:-}" in
  setup)
    python3.12 -m venv "$out/bootstrap"
    "$out/bootstrap/bin/python" -m pip install uv==0.9.28
    python3.12 -m venv "$out/venv"
    "$out/bootstrap/bin/python" -m uv pip sync --python "$env_python" --require-hashes \
      "$root/requirements.lock"
    ;;
  prepare)
    exec "$env_python" -m mjlab_joshua.prepare \
      --project /workspace/output/robotcad/spider_sts3215 --output "$JOSHUA_SPIDER_ASSET"
    ;;
  check)
    exec "$env_python" -m pytest -q -p no:cacheprovider \
      --rootdir "$root/tests" --confcutdir "$root/tests" "$root/tests"
    ;;
  gait-motion)
    shift
    exec "$env_python" -m mjlab_joshua.gait "$@"
    ;;
  smoke|train|evaluate|preview|live)
    module=$1
    shift
    exec "$env_python" -u -m "mjlab_joshua.$module" "$@"
    ;;
  *) echo "Expected setup, prepare, check, gait-motion, smoke, train, evaluate, preview, or live" >&2; exit 2 ;;
esac
