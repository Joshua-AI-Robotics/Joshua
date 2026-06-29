#!/usr/bin/env bash

set -euo pipefail

required_services=(
  test-u22
  test-u24
  run-u22
  run-u24
  build-u22-x86
  build-u22-arm64
  build-u24-x86
  build-u24-arm64
)

docker compose \
  --profile u24 \
  --profile arm64 \
  --profile production \
  --profile tasks \
  config >/dev/null

services=$(docker compose --profile u24 --profile arm64 --profile tasks config --services)

for service in "${required_services[@]}"; do
  if ! grep -Fxq "${service}" <<<"${services}"; then
    echo "Missing Compose task service: ${service}" >&2
    exit 1
  fi
done

isaac_run=$(
  docker compose \
    -f docker-compose.yml \
    -f docker-compose.isaac.yml \
    --profile tasks \
    config run-u24
)

if ! grep -q 'ISAAC_LAB_PATH' <<<"${isaac_run}"; then
  echo "Isaac run task is missing the Isaac overlay environment." >&2
  exit 1
fi

if grep -q 'bazel ' Makefile; then
  echo "Makefile must only alias Compose services; found an embedded Bazel command." >&2
  exit 1
fi

if grep -q 'make_target\|make \${{ matrix' .github/workflows/ci.yml; then
  echo "CI must invoke canonical Compose task services directly." >&2
  exit 1
fi

if [[ -e scripts/build.py ]]; then
  echo "scripts/build.py is an obsolete native Docker wrapper." >&2
  exit 1
fi

if ! grep -Fxq 'dist/' .dockerignore; then
  echo "dist/ must be excluded from Docker build contexts." >&2
  exit 1
fi

echo "Docker entrypoint contract passed."
