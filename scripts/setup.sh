#!/bin/bash
# Joshua host bootstrap.
#
# This script intentionally installs/checks Docker host tooling only. Joshua
# runtime dependencies such as ROS2, Bazel, Python packages, OpenCV, CUDA/CuDNN,
# and project tools are provided by Docker images. NVIDIA Container Toolkit is
# installed by default; --cpu skips GPU host setup for CPU-only development.

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

INSTALL_GPU=true
if [ "$#" -eq 1 ] && [ "$1" = "--cpu" ]; then
    INSTALL_GPU=false
elif [ "$#" -ne 0 ]; then
    echo -e "${RED}Error: setup.sh accepts only the optional --cpu flag.${NC}"
    echo "Usage: sudo ./scripts/setup.sh [--cpu]"
    echo "GPU setup is the default; --cpu skips NVIDIA Container Toolkit installation."
    exit 1
fi

if [ "${EUID}" -ne 0 ]; then
    echo -e "${RED}Error: run this host bootstrap with sudo.${NC}"
    echo "Usage: sudo ./scripts/setup.sh [--cpu]"
    exit 1
fi

if [ -f /etc/os-release ]; then
    . /etc/os-release
else
    echo -e "${RED}Error: cannot detect host OS.${NC}"
    exit 1
fi

if [ "${ID:-}" != "ubuntu" ]; then
    echo -e "${YELLOW}Warning: this bootstrap is tested on Ubuntu Linux hosts.${NC}"
fi

NONROOT_USER="${SUDO_USER:-}"
if [ -z "${NONROOT_USER}" ] || [ "${NONROOT_USER}" = "root" ]; then
    echo -e "${YELLOW}Warning: could not determine the non-root user for docker group setup.${NC}"
fi

echo -e "${GREEN}=== Joshua Docker host bootstrap ===${NC}"
echo -e "${BLUE}Installing Docker Engine, Compose v2, and buildx if needed...${NC}"

# Ubuntu's archive ships its own Docker/Compose packages (docker.io,
# docker-compose, docker-compose-v2, ...) that conflict with the docker.com
# packages installed below -- e.g. docker-compose-plugin and docker-compose-v2
# both ship /usr/libexec/docker/cli-plugins/docker-compose, so dpkg refuses to
# unpack docker-compose-plugin while the Ubuntu package is present. Remove the
# conflicting Ubuntu-archive packages first (no-op if they aren't installed).
# Remove one package per call, as Docker's own docs do. `apt-get remove` is
# atomic: if a single name is missing from the archive -- releases differ, and
# these names have moved between Ubuntu versions -- the whole call aborts with
# "Unable to locate package" and removes *nothing*. The `|| true` would then
# swallow that abort, silently leaving the conflict in place.
CONFLICTING_PACKAGES=(docker.io docker-doc docker-compose docker-compose-v2 podman-docker containerd runc)
for pkg in "${CONFLICTING_PACKAGES[@]}"; do
    apt-get remove -y "$pkg" || true
done

DOCKER_SOURCES=/etc/apt/sources.list.d/docker.sources
LEGACY_DOCKER_LIST=/etc/apt/sources.list.d/docker.list
DOCKER_REPOSITORY=https://download.docker.com/linux/ubuntu

if [ -f "${DOCKER_SOURCES}" ] && [ -f "${LEGACY_DOCKER_LIST}" ] &&
    grep -q "${DOCKER_REPOSITORY}" "${DOCKER_SOURCES}" &&
    grep -q "${DOCKER_REPOSITORY}" "${LEGACY_DOCKER_LIST}"; then
    echo -e "${YELLOW}Removing duplicate legacy Docker apt source: ${LEGACY_DOCKER_LIST}${NC}"
    rm -f "${LEGACY_DOCKER_LIST}"
fi

apt-get update
apt-get install -y ca-certificates curl gnupg lsb-release

install -m 0755 -d /etc/apt/keyrings
if ! grep -Rqs "${DOCKER_REPOSITORY}" /etc/apt/sources.list /etc/apt/sources.list.d; then
    curl -fsSL "${DOCKER_REPOSITORY}/gpg" -o /etc/apt/keyrings/docker.asc
    chmod a+r /etc/apt/keyrings/docker.asc
    cat > "${DOCKER_SOURCES}" <<EOF
Types: deb
URIs: ${DOCKER_REPOSITORY}
Suites: ${UBUNTU_CODENAME:-${VERSION_CODENAME}}
Components: stable
Architectures: $(dpkg --print-architecture)
Signed-By: /etc/apt/keyrings/docker.asc
EOF
fi

apt-get update
apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

if [ -n "${NONROOT_USER}" ] && [ "${NONROOT_USER}" != "root" ]; then
    usermod -aG docker "${NONROOT_USER}"
fi

systemctl enable --now docker >/dev/null 2>&1 || true

if [ "${INSTALL_GPU}" = true ]; then
    echo -e "${BLUE}Validating the host NVIDIA driver...${NC}"
    if ! command -v nvidia-smi >/dev/null 2>&1 || ! nvidia-smi -L >/dev/null 2>&1; then
        echo -e "${RED}Error: default setup requires a working NVIDIA driver and a GPU visible to nvidia-smi.${NC}"
        echo "Install or repair the NVIDIA driver, or use sudo ./scripts/setup.sh --cpu for CPU-only development."
        exit 1
    fi

    echo -e "${BLUE}Installing NVIDIA Container Toolkit...${NC}"
    NVIDIA_KEYRING=/etc/apt/keyrings/nvidia-container-toolkit-keyring.gpg
    NVIDIA_LIST=/etc/apt/sources.list.d/nvidia-container-toolkit.list

    if [ ! -f "${NVIDIA_KEYRING}" ]; then
        curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey \
            | gpg --dearmor -o "${NVIDIA_KEYRING}"
        chmod a+r "${NVIDIA_KEYRING}"
    fi

    curl -fsSL https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list \
        | sed "s#deb https://#deb [signed-by=${NVIDIA_KEYRING}] https://#g" \
        > "${NVIDIA_LIST}"

    apt-get update
    apt-get install -y nvidia-container-toolkit
    nvidia-ctk runtime configure --runtime=docker
    systemctl restart docker
fi

echo -e "${BLUE}Verifying Docker tooling...${NC}"
docker --version
docker compose version
docker buildx version

if [ "${INSTALL_GPU}" = true ]; then
    nvidia-ctk --version
    if ! docker info --format '{{json .Runtimes}}' | grep -q 'nvidia'; then
        echo -e "${RED}Error: Docker does not report the NVIDIA runtime after configuration.${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}Docker host bootstrap complete.${NC}"
echo -e "${YELLOW}If this user was newly added to the docker group, log out and back in before running Docker without sudo.${NC}"
echo
echo "Next commands:"
echo "  docker compose run --rm test-u22"
echo "  docker compose run --rm test-u24"
if [ "${INSTALL_GPU}" = true ]; then
    echo "  docker compose run --rm joshua-u22 nvidia-smi"
    echo "  docker compose run --rm joshua-u24"
else
    echo "  docker compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm joshua-u22"
    echo "  docker compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm joshua-u24"
fi
echo "Optional shorthand: make help"
