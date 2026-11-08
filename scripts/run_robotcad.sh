#!/usr/bin/env bash
# Run CAD utilities in Docker; installation and all generated files are ignored.
set -euo pipefail

CAD_REPO=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
CAD_TOOLS=$CAD_REPO/tools/cad
CAD_ROOT=$CAD_REPO/output/robotcad
CAD_CONTAINER_ROOT=/workspace/output/robotcad
CAD_IMAGE=${JOSHUA_CAD_IMAGE:-joshua:u24-jazzy}
CAD_MODE=open
CAD_ROBOT=demo
CAD_DOCKER_ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --robot)
            if [[ $# -lt 2 || ( $2 != demo && $2 != spider ) ]]; then
                echo '--robot requires demo or spider.' >&2
                exit 2
            fi
            CAD_ROBOT=$2
            shift
            ;;
        --create) set -- --create-demo "${@:2}"; continue ;;
        --create-demo|--verify-exports|--convert-mujoco|--mujoco|--check-cad|--package)
            if [[ $CAD_MODE != open ]]; then
                echo 'Select only one operation.' >&2
                exit 2
            fi
            CAD_MODE=$1
            ;;
        --detach) CAD_DOCKER_ARGS+=(-d) ;;
        --help)
            echo 'Usage: scripts/run_robotcad.sh [--robot demo|spider] [--detach] [--create | --verify-exports | --convert-mujoco | --mujoco | --check-cad | --package]'
            echo 'Default: open the saved demo; --create-demo refuses to overwrite it.'
            exit 0
            ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
if [[ ( $CAD_MODE == --check-cad || $CAD_MODE == --package ) && $CAD_ROBOT != spider ]]; then
    echo '--check-cad and --package currently require --robot spider.' >&2
    exit 2
fi
CAD_PROJECT=$CAD_CONTAINER_ROOT/project
CAD_PROJECT_HOST=$CAD_ROOT/project
CAD_DOCUMENT=robotcad_demo.FCStd
CAD_BUILD_RESULT=$CAD_ROOT/build-result.json
if [[ $CAD_ROBOT == spider ]]; then
    CAD_PROJECT=$CAD_CONTAINER_ROOT/spider_sts3215
    CAD_PROJECT_HOST=$CAD_ROOT/spider_sts3215
    CAD_DOCUMENT=spider_sts3215.FCStd
    CAD_BUILD_RESULT=$CAD_PROJECT_HOST/build-result.json
fi
if [[ $CAD_MODE != open && $CAD_MODE != --mujoco && ${#CAD_DOCKER_ARGS[@]} != 0 ]]; then
    echo '--detach is only available for the editor or MuJoCo viewer.' >&2
    exit 2
fi
if [[ ! -x "$CAD_ROOT/squashfs-root/usr/bin/freecad" ]]; then
    echo 'Run scripts/setup_robotcad.sh first.' >&2
    exit 1
fi
if [[ $CAD_MODE == --create-demo && -e "$CAD_PROJECT_HOST/$CAD_DOCUMENT" ]]; then
    echo "CAD already exists under $CAD_PROJECT_HOST; preserve it before recreating." >&2
    exit 1
fi
CAD_COMMON_ARGS=(
    --rm --init --network none --cap-drop ALL --security-opt no-new-privileges
    --user "$(id -u):$(id -g)" --shm-size 256m --entrypoint /bin/bash
    --mount "type=bind,src=$CAD_ROOT,dst=$CAD_CONTAINER_ROOT"
    --mount "type=bind,src=$CAD_TOOLS,dst=/workspace/tools/cad,readonly"
    --workdir "$CAD_CONTAINER_ROOT" -e "HOME=$CAD_CONTAINER_ROOT/home"
    -e "JOSHUA_CAD_ROOT=$CAD_CONTAINER_ROOT"
    -e "JOSHUA_CAD_PROJECT=$CAD_PROJECT" -e "JOSHUA_CAD_DOCUMENT=$CAD_DOCUMENT"
)
if [[ $CAD_MODE == --verify-exports || $CAD_MODE == --convert-mujoco || $CAD_MODE == --package ]]; then
    CAD_PYTHON_SCRIPT=/workspace/tools/cad/verify_exports.py
    if [[ $CAD_MODE == --convert-mujoco ]]; then
        CAD_PYTHON_SCRIPT=/workspace/tools/cad/convert_mujoco.py
    fi
    if [[ $CAD_ROBOT == spider ]]; then
        CAD_PYTHON_SCRIPT=/workspace/tools/cad/verify_spider.py
        if [[ $CAD_MODE == --convert-mujoco ]]; then
            CAD_PYTHON_SCRIPT=/workspace/tools/cad/convert_spider.py
        elif [[ $CAD_MODE == --package ]]; then
            CAD_PYTHON_SCRIPT=/workspace/tools/cad/package_spider.py
        fi
    fi
    exec docker run "${CAD_COMMON_ARGS[@]}" \
        -e MUJOCO_GL=egl -e LIBGL_ALWAYS_SOFTWARE=1 \
        -e "PYTHONPATH=$CAD_CONTAINER_ROOT/home/.local/share/FreeCAD/AdditionalPythonPackages/py311" \
        "$CAD_IMAGE" -c 'exec ./squashfs-root/AppRun python "$1"' bash "$CAD_PYTHON_SCRIPT"
fi
if [[ -z ${DISPLAY:-} ]]; then
    echo 'Run this from a graphical desktop terminal (DISPLAY is unset).' >&2
    exit 1
fi
CAD_AUTH_SOURCE=${XAUTHORITY:-$HOME/.Xauthority}
if [[ ! -r "$CAD_AUTH_SOURCE" ]]; then
    echo 'Set XAUTHORITY to the readable X11 authority file for your desktop session.' >&2
    exit 1
fi
cp -- "$CAD_AUTH_SOURCE" "$CAD_ROOT/Xauthority.source"
chmod 600 "$CAD_ROOT/Xauthority.source"
touch "$CAD_ROOT/Xauthority"
chmod 600 "$CAD_ROOT/Xauthority"
xauth -f "$CAD_ROOT/Xauthority.source" nlist "$DISPLAY" |
    sed 's/^..../ffff/' | xauth -f "$CAD_ROOT/Xauthority" nmerge -
mkdir -p "$CAD_ROOT/home/runtime"
chmod 700 "$CAD_ROOT/home/runtime"
CAD_MACRO=/workspace/tools/cad/open_demo.FCMacro
CAD_CONTAINER_NAME=joshua-freecad-robotcad
CAD_LAUNCH_ARGS=("$CAD_MACRO")
if [[ $CAD_MODE == --create-demo ]]; then
    CAD_LAUNCH_ARGS=(/workspace/tools/cad/create_demo.FCMacro)
    if [[ $CAD_ROBOT == spider ]]; then
        CAD_LAUNCH_ARGS=(/workspace/tools/cad/create_spider.FCMacro)
    fi
elif [[ $CAD_MODE == --check-cad ]]; then
    CAD_LAUNCH_ARGS=(/workspace/tools/cad/check_spider.FCMacro)
    CAD_CONTAINER_NAME=joshua-robotcad-clearance
elif [[ $CAD_MODE == --mujoco ]]; then
    if [[ ! -f "$CAD_PROJECT_HOST/mujoco/scene.xml" ]]; then
        echo "Run scripts/run_robotcad.sh --robot $CAD_ROBOT --convert-mujoco first." >&2
        exit 1
    fi
    CAD_CONTAINER_NAME=joshua-robotcad-mujoco
    CAD_LAUNCH_ARGS=(python -m mujoco.viewer "--mjcf=$CAD_PROJECT/mujoco/scene.xml")
fi
if [[ $CAD_ROBOT == spider ]]; then
    CAD_CONTAINER_NAME=$CAD_CONTAINER_NAME-spider
fi
docker run --name "$CAD_CONTAINER_NAME" \
    "${CAD_DOCKER_ARGS[@]}" "${CAD_COMMON_ARGS[@]}" \
    --mount type=bind,src=/tmp/.X11-unix,dst=/tmp/.X11-unix,readonly \
    -e "XDG_RUNTIME_DIR=$CAD_CONTAINER_ROOT/home/runtime" \
    -e "DISPLAY=$DISPLAY" -e "XAUTHORITY=$CAD_CONTAINER_ROOT/Xauthority" \
    -e "PYTHONPATH=$CAD_CONTAINER_ROOT/home/.local/share/FreeCAD/AdditionalPythonPackages/py311" \
    -e LIBGL_ALWAYS_SOFTWARE=1 -e QTWEBENGINE_DISABLE_SANDBOX=1 \
    "$CAD_IMAGE" -c 'exec ./squashfs-root/AppRun "$@"' bash "${CAD_LAUNCH_ARGS[@]}"
if [[ $CAD_MODE == --create-demo && ! -f "$CAD_BUILD_RESULT" ]]; then
    echo "Generation failed; inspect $CAD_PROJECT_HOST and output/robotcad for build-error.txt." >&2
    exit 1
fi
if [[ $CAD_MODE == --check-cad ]]; then
    if [[ ! -f "$CAD_PROJECT_HOST/clearance-validation.json" ]] || ! grep -q '"status": "ok"' "$CAD_PROJECT_HOST/clearance-validation.json"; then
        echo "Clearance check needs review; inspect $CAD_PROJECT_HOST/clearance-validation.json and clearance-error.txt." >&2
        exit 1
    fi
fi
