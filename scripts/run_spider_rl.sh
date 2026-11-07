#!/usr/bin/env bash
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo"
export JOSHUA_RL_UID="$(id -u)"
export JOSHUA_RL_GID="$(id -g)"
mkdir -p output/spider_rl
case "${1:-}" in
  setup|prepare|check|cpu-smoke) service=$1; shift; args=() ;;
  smoke) service=smoke; shift; args=(smoke) ;;
  *) echo "Usage: $0 {setup|prepare|check|cpu-smoke|smoke [--num-envs 64]}" >&2; exit 2 ;;
esac
exec docker compose -p joshua-spider-rl -f docker-compose.spider-rl.yml \
  run --rm "$service" "${args[@]}" "$@"
