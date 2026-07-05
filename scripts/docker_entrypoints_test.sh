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

if [[ -e docker-compose.gpu.yml ]]; then
  echo "docker-compose.gpu.yml is obsolete because GPU access is the default." >&2
  exit 1
fi

default_compose=(
  docker compose
  --profile u24
  --profile tasks
)

for service in joshua-u22 joshua-u24 run-u22 run-u24; do
  gpu_service=$("${default_compose[@]}" config "${service}" | sed '/^x-/,$d')
  if ! grep -q 'driver: nvidia' <<<"${gpu_service}" ||
    ! grep -q -- '- gpu' <<<"${gpu_service}"; then
    echo "Default Compose service ${service} does not reserve an NVIDIA GPU." >&2
    exit 1
  fi
done

for service in test-u22 test-u24 build-u22-x86 build-u24-x86; do
  cpu_service=$("${default_compose[@]}" config "${service}" | sed '/^x-/,$d')
  if grep -q 'driver: nvidia' <<<"${cpu_service}"; then
    echo "CPU-only Compose service ${service} unexpectedly reserves an NVIDIA GPU." >&2
    exit 1
  fi
done

if [[ ! -f docker-compose.cpu.yml ]]; then
  echo "Missing explicit CPU Compose override: docker-compose.cpu.yml" >&2
  exit 1
fi

cpu_compose=(
  docker compose
  -f docker-compose.yml
  -f docker-compose.cpu.yml
  --profile u24
  --profile tasks
)

"${cpu_compose[@]}" config >/dev/null

for service in joshua-u22 joshua-u24 run-u22 run-u24; do
  cpu_service=$("${cpu_compose[@]}" config "${service}" | sed '/^x-/,$d')
  if grep -q 'driver: nvidia' <<<"${cpu_service}"; then
    echo "CPU Compose override did not clear the GPU from ${service}." >&2
    exit 1
  fi
done

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

if ! grep -q -- '--cpu' scripts/setup.sh; then
  echo "scripts/setup.sh must support the explicit --cpu fallback." >&2
  exit 1
fi

if grep -Eq 'docker\.gpg|^DOCKER_LIST=.*/docker\.list' scripts/setup.sh; then
  echo "setup.sh must not create the legacy Docker apt source that conflicts with docker.sources." >&2
  exit 1
fi

if ! grep -q 'docker\.sources' scripts/setup.sh ||
  ! grep -q 'LEGACY_DOCKER_LIST' scripts/setup.sh; then
  echo "setup.sh must use the current Docker source format and reconcile a legacy duplicate." >&2
  exit 1
fi

if ! grep -q 'docker-compose.cpu.yml' docs/GETTING_STARTED.md; then
  echo "Getting Started must document the explicit CPU Compose override." >&2
  exit 1
fi

if ! grep -Fxq 'dist/' .dockerignore; then
  echo "dist/ must be excluded from Docker build contexts." >&2
  exit 1
fi

required_python_packages=$(
  find . -type f \( -name 'BUILD' -o -name 'BUILD.*' \) \
    -exec grep -hoE 'requirement\("[^"]+"\)' {} + \
    | sed -E 's/requirement\("([^"]+)"\)/\1/' \
    | sort -u
)

for lockfile in requirements.lock requirements_3.12.lock; do
  while IFS= read -r package; do
    normalized_package=${package,,}
    normalized_package=${normalized_package//_/-}
    if ! grep -Eiq "^${normalized_package}==" "${lockfile}"; then
      echo "${lockfile} is missing Bazel requirement: ${package}" >&2
      exit 1
    fi
  done <<<"${required_python_packages}"
done

echo "Docker entrypoint contract passed."
