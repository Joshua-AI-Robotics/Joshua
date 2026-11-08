#!/usr/bin/env bash
# Install local CAD tooling; all downloaded/runtime files stay under output/.
set -euo pipefail

if [[ ${1:-} == --help ]]; then
    echo 'Usage: scripts/setup_robotcad.sh'
    echo 'Install pinned FreeCAD/RobotCAD under output/robotcad using joshua:u24-jazzy.'
    exit 0
fi
if [[ $# != 0 ]]; then
    echo 'Usage: scripts/setup_robotcad.sh' >&2
    exit 2
fi
CAD_REPO=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
CAD_TOOLS=$CAD_REPO/tools/cad
CAD_ROOT=$CAD_REPO/output/robotcad
CAD_CONTAINER_ROOT=/workspace/output/robotcad
CAD_IMAGE=${JOSHUA_CAD_IMAGE:-joshua:u24-jazzy}
source "$CAD_TOOLS/versions.env"

case "$(uname -s):$(uname -m)" in
    Linux:x86_64) ;;
    *) echo 'This pinned FreeCAD AppImage requires x86_64 Linux.' >&2; exit 1 ;;
esac
for CAD_TOOL in docker curl git sha256sum; do
    command -v "$CAD_TOOL" >/dev/null || { echo "Required command: $CAD_TOOL" >&2; exit 1; }
done
if ! docker image inspect "$CAD_IMAGE" >/dev/null 2>&1; then
    echo "Docker image $CAD_IMAGE is unavailable. Run docker compose build joshua-u24 first." >&2
    exit 1
fi
mkdir -p "$CAD_ROOT"
CAD_APPIMAGE_PATH=$CAD_ROOT/$FREECAD_APPIMAGE
if [[ ! -f "$CAD_APPIMAGE_PATH" ]]; then
    curl --fail --location --retry 3 \
        "https://github.com/FreeCAD/FreeCAD/releases/download/$FREECAD_VERSION/$FREECAD_APPIMAGE" \
        -o "$CAD_APPIMAGE_PATH.part"
    mv -- "$CAD_APPIMAGE_PATH.part" "$CAD_APPIMAGE_PATH"
fi
printf '%s  %s\n' "$FREECAD_SHA256" "$CAD_APPIMAGE_PATH" | sha256sum --check -
chmod +x "$CAD_APPIMAGE_PATH"

if [[ ! -d "$CAD_ROOT/addon" ]]; then
    git clone --no-checkout https://github.com/drfenixion/freecad.robotcad.git "$CAD_ROOT/addon"
    git -C "$CAD_ROOT/addon" checkout --detach "$ROBOTCAD_REVISION"
fi
if [[ $(git -C "$CAD_ROOT/addon" rev-parse HEAD) != "$ROBOTCAD_REVISION" ]]; then
    echo "Existing RobotCAD checkout differs from pinned revision $ROBOTCAD_REVISION; preserve local changes before updating it." >&2
    exit 1
fi
if [[ -n $(git -C "$CAD_ROOT/addon" status --porcelain) ]]; then
    echo 'Existing RobotCAD checkout has local changes; preserve them before setup.' >&2
    exit 1
fi
for CAD_MOD_DIR in "$CAD_ROOT/home/.local/share/FreeCAD/Mod" "$CAD_ROOT/home/.local/share/FreeCAD/v1-1/Mod"; do
    mkdir -p "$CAD_MOD_DIR"
    if [[ -e "$CAD_MOD_DIR/freecad.robotcad" && ! -L "$CAD_MOD_DIR/freecad.robotcad" ]]; then
        echo "Refusing to replace existing directory: $CAD_MOD_DIR/freecad.robotcad" >&2
        exit 1
    fi
    ln -sfn "$CAD_CONTAINER_ROOT/addon" "$CAD_MOD_DIR/freecad.robotcad"
done

CAD_DOCKER_ARGS=(
    --rm --user "$(id -u):$(id -g)" --entrypoint /bin/bash
    --mount "type=bind,src=$CAD_ROOT,dst=$CAD_CONTAINER_ROOT"
    --mount "type=bind,src=$CAD_TOOLS,dst=/workspace/tools/cad,readonly"
    --workdir "$CAD_CONTAINER_ROOT" -e "HOME=$CAD_CONTAINER_ROOT/home"
)
if [[ ! -x "$CAD_ROOT/squashfs-root/usr/bin/freecad" ]]; then
    docker run --network none "${CAD_DOCKER_ARGS[@]}" "$CAD_IMAGE" \
        -c '"./$1" --appimage-extract > extraction.log' bash "$FREECAD_APPIMAGE"
fi
CAD_DEPENDENCY_STAMP=$CAD_ROOT/dependencies.sha256
CAD_DEPENDENCY_KEY=$(sha256sum "$CAD_TOOLS/requirements.lock" "$CAD_TOOLS/versions.env" | sha256sum | cut -d ' ' -f 1)
if [[ ! -f "$CAD_DEPENDENCY_STAMP" || $(cat "$CAD_DEPENDENCY_STAMP") != "$CAD_DEPENDENCY_KEY" ]]; then
    docker run "${CAD_DOCKER_ARGS[@]}" "$CAD_IMAGE" -c \
        './squashfs-root/AppRun python -m pip install --disable-pip-version-check --upgrade --target "$HOME/.local/share/FreeCAD/AdditionalPythonPackages/py311" -r /workspace/tools/cad/requirements.lock'
    printf '%s\n' "$CAD_DEPENDENCY_KEY" > "$CAD_DEPENDENCY_STAMP"
fi
echo 'FreeCAD/RobotCAD is ready. Run scripts/run_robotcad.sh --create-demo once, then scripts/run_robotcad.sh.'
