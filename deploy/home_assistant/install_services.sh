#!/bin/bash
# Install persistent services. Intentionally does not start capture.
set -euo pipefail
if [[ $EUID != 0 || $# != 4 ]]; then
  echo "Usage: sudo bash $0 PACKAGE_DIRECTORY CONFIG_FILE HA_DIRECTORY USER" >&2
  exit 2
fi
package=$1
config=$2
ha_dir=$3
run_user=$4
# Keep systemd unit values unambiguous; reject shell/unit metacharacters.
for path in "$package" "$config" "$ha_dir"; do
  if [[ ! "$path" =~ ^/[a-zA-Z0-9_./-]+$ ]]; then
    echo "Use absolute paths without spaces or special characters: $path" >&2
    exit 2
  fi
done
[[ "$run_user" =~ ^[a-z_][a-z0-9_-]*$ && "$run_user" != root ]]
id "$run_user" >/dev/null
[[ -x "$package/joshua_monitor_main" && -f "$config" ]]
[[ -f "$ha_dir/compose.yaml" && -f "$ha_dir/webhook.secret" ]]
[[ -f /opt/ros/jazzy/setup.bash ]]
for command in docker systemctl curl tailscale; do command -v "$command" >/dev/null; done
docker compose version >/dev/null
runuser -u "$run_user" -- test -r "$config"
runuser -u "$run_user" -- test -r "$ha_dir/webhook.secret"
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
install -D -m 0755 "$script_dir/run_monitor.sh" /usr/local/lib/joshua/run_monitor.sh
cat >/etc/joshua-monitor.env <<EOF
JOSHUA_PACKAGE=$package
JOSHUA_CONFIG=$config
JOSHUA_HA_DIR=$ha_dir
EOF
chmod 0644 /etc/joshua-monitor.env
cat >/etc/systemd/system/joshua-home-assistant.service <<EOF
[Unit]
Description=Joshua Home Assistant and WebRTC relay
Requires=docker.service
After=docker.service network-online.target tailscaled.service
Wants=network-online.target tailscaled.service

[Service]
Type=oneshot
RemainAfterExit=yes
WorkingDirectory=$ha_dir
ExecStartPre=/usr/bin/tailscale wait
ExecStart=/usr/bin/docker compose up -d --wait --wait-timeout 120
TimeoutStartSec=180

[Install]
WantedBy=multi-user.target
EOF
cat >/etc/systemd/system/joshua-monitor.service <<EOF
[Unit]
Description=Joshua camera, audio, inference, streaming and notifications
Requires=joshua-home-assistant.service
After=joshua-home-assistant.service network-online.target tailscaled.service
Wants=network-online.target tailscaled.service
StartLimitIntervalSec=0

[Service]
Type=simple
User=$run_user
EnvironmentFile=/etc/joshua-monitor.env
ExecStart=/usr/local/lib/joshua/run_monitor.sh
Restart=always
RestartSec=5
KillMode=mixed
KillSignal=SIGINT
TimeoutStopSec=20

[Install]
WantedBy=multi-user.target
EOF
systemd-analyze verify /etc/systemd/system/joshua-{home-assistant,monitor}.service
systemctl daemon-reload
systemctl enable docker.service tailscaled.service joshua-home-assistant.service joshua-monitor.service
echo "Installed and enabled. After stopping old Joshua processes, run:"
echo "  sudo systemctl start joshua-monitor"
