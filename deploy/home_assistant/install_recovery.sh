#!/bin/bash
# Opt-in host recovery; does not reboot or restart the camera pipeline.
set -euo pipefail
[[ $EUID == 0 ]] || { echo 'Run with sudo.' >&2; exit 2; }
for command in python3 ip ping tailscale networkctl systemctl; do
  command -v "$command" >/dev/null
done
[[ -e /sys/class/watchdog/watchdog0/identity ]]
systemctl is-active --quiet systemd-networkd
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
install -D -m 0755 "$script_dir/network_recovery.py" /usr/local/lib/joshua/network_recovery.py
install -m 0644 "$script_dir/joshua-network-recovery.service" /etc/systemd/system/
install -m 0644 "$script_dir/joshua-network-recovery.timer" /etc/systemd/system/
systemd-analyze verify /etc/systemd/system/joshua-network-recovery.{service,timer}
mkdir -p /etc/systemd/system.conf.d
cat >/etc/systemd/system.conf.d/70-joshua-watchdog.conf <<'EOF'
[Manager]
RuntimeWatchdogSec=15s
RebootWatchdogSec=2min
EOF
systemctl daemon-reexec
systemctl enable --now joshua-network-recovery.timer
systemctl show --property=RuntimeWatchdogUSec --property=RebootWatchdogUSec
