#!/bin/bash
# Installed by install_services.sh. Runs as the capture user, not root.
set -eo pipefail
: "${JOSHUA_PACKAGE:?}" "${JOSHUA_CONFIG:?}" "${JOSHUA_HA_DIR:?}"
export JOSHUA_HA_WEBHOOK_FILE="$JOSHUA_HA_DIR/webhook.secret"
source /opt/ros/jazzy/setup.bash

# Docker's process may start before HA finishes loading integrations. Tailscale
# also needs its address before the media server can bind its private listener.
ready=false
for ((attempt=0; attempt<120; attempt++)); do
  if curl --silent --fail --max-time 2 http://127.0.0.1:8123/ >/dev/null &&
     tailscale ip -4 2>/dev/null | grep -q '^100\.'; then
    ready=true
    break
  fi
  sleep 1
done
if [[ "$ready" != true ]]; then
  echo "Home Assistant or Tailscale is not ready; systemd will retry." >&2
  exit 1
fi
cd "$JOSHUA_PACKAGE"
exec ./joshua_monitor_main --config="$JOSHUA_CONFIG" --exit_on_node_failure
