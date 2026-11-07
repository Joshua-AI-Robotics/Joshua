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
  train|evaluate) service=smoke; args=("$1"); shift ;;
  preview) service=smoke; args=(preview record); shift ;;
  watch-video) service=smoke; args=(live); shift ;;
  gait-motion) service=check; args=(gait-motion); shift ;;
  view|watch)
    viewer_name=joshua-spider-policy-viewer
    viewer_args=(-u -m mjlab_joshua.preview play)
    if [[ $1 == watch ]]; then
      viewer_name=joshua-spider-training-viewer
      viewer_args=(-u -m mjlab_joshua.live)
    fi
    shift
    detach=()
    if [[ ${1:-} == --detach ]]; then detach=(-d); shift; fi
    auth=${XAUTHORITY:-$HOME/.Xauthority}
    if [[ -z ${DISPLAY:-} || ! -r $auth ]]; then
      echo 'view/watch needs a desktop DISPLAY and readable XAUTHORITY.' >&2
      exit 1
    fi
    touch output/spider_rl/Xauthority
    chmod 600 output/spider_rl/Xauthority
    xauth -f "$auth" nlist "$DISPLAY" |
      sed 's/^..../ffff/' | xauth -f output/spider_rl/Xauthority nmerge -
    exec docker run --rm --init "${detach[@]}" --name "$viewer_name" \
      --network none --cap-drop ALL --security-opt no-new-privileges \
      --user "$JOSHUA_RL_UID:$JOSHUA_RL_GID" \
      --mount "type=bind,src=$repo,dst=/workspace,readonly" \
      --mount "type=bind,src=$repo/output,dst=/workspace/output" \
      --mount type=bind,src=/tmp/.X11-unix,dst=/tmp/.X11-unix,readonly \
      -e "DISPLAY=$DISPLAY" -e XAUTHORITY=/workspace/output/spider_rl/Xauthority \
      -e HOME=/workspace/output/spider_rl/home \
      -e PYTHONPATH=/workspace/ai/train/locomotion -e PYTHONDONTWRITEBYTECODE=1 \
      -e MUJOCO_GL=glfw -e LIBGL_ALWAYS_SOFTWARE=1 \
      --entrypoint /workspace/output/spider_rl/venv/bin/python \
      "${JOSHUA_RL_IMAGE:-joshua:u24-jazzy}" \
      "${viewer_args[@]}" "$@"
    ;;
  *) echo "Usage: $0 {setup|prepare|check|gait-motion|cpu-smoke|smoke|train|evaluate|preview|view|watch|watch-video} [options]" >&2; exit 2 ;;
esac
exec docker compose -p joshua-spider-rl -f docker-compose.spider-rl.yml \
  run --rm "$service" "${args[@]}" "$@"
