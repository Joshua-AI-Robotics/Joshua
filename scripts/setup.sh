#!/bin/bash
# Joshua host bootstrap.
#
# This script intentionally installs/checks Docker host tooling only. Joshua
# runtime dependencies such as ROS2, Bazel, Python packages, OpenCV, CUDA/CuDNN,
# and project tools are provided by Docker images.

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

if [ "${EUID}" -ne 0 ]; then
    echo -e "${RED}Error: run this host bootstrap with sudo.${NC}"
    echo "Usage: sudo ./scripts/setup.sh"
    exit 1
fi

if [ "$#" -ne 0 ]; then
    echo -e "${RED}Error: setup.sh no longer accepts native setup modes.${NC}"
    echo "Usage: sudo ./scripts/setup.sh"
    echo "Joshua dependencies are installed inside Docker images; this script only prepares Docker host tooling."
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

apt-get update
apt-get install -y ca-certificates curl gnupg lsb-release

install -m 0755 -d /etc/apt/keyrings
if [ ! -f /etc/apt/keyrings/docker.gpg ]; then
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | gpg --dearmor -o /etc/apt/keyrings/docker.gpg
    chmod a+r /etc/apt/keyrings/docker.gpg
fi

DOCKER_LIST=/etc/apt/sources.list.d/docker.list
if [ ! -f "${DOCKER_LIST}" ]; then
    echo \
        "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu ${VERSION_CODENAME} stable" \
        > "${DOCKER_LIST}"
fi

apt-get update
apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

if [ -n "${NONROOT_USER}" ] && [ "${NONROOT_USER}" != "root" ]; then
    usermod -aG docker "${NONROOT_USER}"
fi

systemctl enable --now docker >/dev/null 2>&1 || true

echo -e "${BLUE}Verifying Docker tooling...${NC}"
docker --version
docker compose version
docker buildx version

echo -e "${GREEN}Docker host bootstrap complete.${NC}"
echo -e "${YELLOW}If this user was newly added to the docker group, log out and back in before running Docker without sudo.${NC}"
echo
echo "Next commands:"
echo "  docker compose run --rm joshua-u22"
echo "  docker compose run --rm joshua-u24"
echo "  docker compose run --rm test-u22"
echo "  docker compose run --rm test-u24"
echo "Optional shorthand: make help"
