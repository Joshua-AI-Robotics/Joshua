#!/usr/bin/env python3
"""Host network recovery for the netplan/networkd deployment; no ROS ownership.

Run by a systemd timer. --check only probes, without state writes or recovery.
Persistent state prevents reboot loops even across boots or clock rollback.
"""

import argparse
import concurrent.futures
import fcntl
import json
import os
import re
import socket
import subprocess
import time
from pathlib import Path


def command(*args):
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=12)
        return result.returncode == 0, result.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return False, ""


def internet_probe(address):
    try:
        with socket.create_connection((address, 443), timeout=3):
            return True
    except OSError:
        return False


def observe(interface):
    ok, output = command("ip", "-j", "-4", "route", "show", "default")
    routes = json.loads(output) if ok else []
    routes.sort(key=lambda route: route.get("metric", 0))
    route = routes[0] if routes else {}
    interface = route.get("dev", interface)
    gateway = route.get("gateway")
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        internet = any(list(pool.map(internet_probe, ["1.1.1.1", "8.8.8.8"])))
    local = internet
    if not local and gateway:
        local, _ = command("ping", "-n", "-c", "2", "-W", "2", "-I", interface, gateway)
        if not local:
            # A router may block ICMP but still answer a fresh ARP request.
            ok, output = command(
                "ip", "-j", "neigh", "show", "to", gateway, "dev", interface
            )
            local = ok and any(
                "REACHABLE" in row.get("state", []) for row in json.loads(output)
            )
    ok, output = command("tailscale", "status", "--json")
    tail = json.loads(output) if ok else {}
    backend = tail.get("BackendState", "Unknown")
    tail_ok = backend == "Running" and bool(tail.get("Self", {}).get("Online"))
    return {
        "local_ok": local,
        "internet_ok": internet,
        "tailscale_ok": tail_ok,
        "tailscale_state": backend,
        "interface": interface,
    }


def decide(state, health, now, uptime, boot_id):
    """Update state and return at most one action. No side effects here."""
    if state.get("boot_id") != boot_id:
        # Wall time may step at boot. Only the cross-boot reboot guard uses it.
        state = {
            "boot_id": boot_id,
            "last_reboot": state.get("last_reboot"),
            "rebooted_outage": state.get("rebooted_outage", False),
        }
    else:
        state = dict(state)
    if uptime < 180:
        return state, "boot_grace"
    if health["local_ok"]:
        state.pop("local_failed_since", None)
        state.pop("reconnected", None)
        state.setdefault("healthy_since", uptime)
        if uptime - state["healthy_since"] >= 300:
            state["rebooted_outage"] = False
    else:
        state.pop("healthy_since", None)
        state.pop("tail_failed_since", None)
        state.setdefault("local_failed_since", uptime)
        elapsed = uptime - state["local_failed_since"]
        if elapsed >= 120 and not state.get("reconnected"):
            state["reconnected"] = True
            state["last_reconnect"] = uptime
            return state, "reconnect"
        last = state.get("last_reboot")
        if elapsed >= 300 and not state.get("rebooted_outage"):
            if last is None or now - last >= 3600:
                state.update(last_reboot=now, rebooted_outage=True)
                return state, "reboot"
        # While reboot-limited, continue occasional network repair attempts.
        if elapsed >= 300 and uptime - state.get("last_reconnect", 0) >= 600:
            state["last_reconnect"] = uptime
            return state, "reconnect"
        return state, "local_network_down"
    if not health["internet_ok"]:
        state.pop("tail_failed_since", None)
        return state, "upstream_unreachable"
    if health["tailscale_state"] in ("Stopped", "NeedsLogin", "NeedsMachineAuth"):
        state.pop("tail_failed_since", None)
        return state, "tailscale_needs_attention"
    if health["tailscale_ok"]:
        state.pop("tail_failed_since", None)
        return state, "healthy"
    state.setdefault("tail_failed_since", uptime)
    last_restart = state.get("last_tail_restart")
    if uptime - state["tail_failed_since"] >= 120 and (
        last_restart is None or uptime - last_restart >= 600
    ):
        state["last_tail_restart"] = uptime
        return state, "restart_tailscale"
    return state, "tailscale_unavailable"


def save_state(path, state):
    temporary = path.with_suffix(".tmp")
    with temporary.open("w") as stream:
        json.dump(state, stream)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)
    fd = os.open(path.parent, os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def recover(action, interface):
    if action == "restart_tailscale":
        return command("systemctl", "restart", "tailscaled.service")[0]
    if action == "reboot":
        # Normal systemd shutdown lets the recorder finalize its current clip.
        return command("systemctl", "reboot")[0]
    if action == "reconnect":
        if not re.fullmatch(r"[a-zA-Z0-9_-]+", interface):
            raise ValueError("Invalid network interface")
        results = []
        if Path("/sys/class/net", interface, "wireless").exists():
            results.append(
                command("systemctl", "restart", f"netplan-wpa-{interface}.service")[0]
            )
        results.append(command("networkctl", "reconfigure", interface)[0])
        results.append(command("networkctl", "renew", interface)[0])
        return all(results)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--interface", default="wlan0")
    parser.add_argument(
        "--state",
        type=Path,
        default=Path("/var/lib/joshua-network-recovery/state.json"),
    )
    args = parser.parse_args()
    if args.check:
        print(json.dumps(observe(args.interface), sort_keys=True))
        return
    args.state.parent.mkdir(parents=True, exist_ok=True)
    with (args.state.parent / "lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        # Corrupt state fails closed rather than discarding the reboot guard.
        previous = json.loads(args.state.read_text()) if args.state.exists() else {}
        health = observe(args.interface)
        uptime = float(Path("/proc/uptime").read_text().split()[0])
        boot_id = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
        state, action = decide(previous, health, time.time(), uptime, boot_id)
        if state != previous:
            save_state(args.state, state)
        print(json.dumps({"action": action, **health}, sort_keys=True), flush=True)
        if not recover(action, health["interface"]):
            raise RuntimeError(
                f"Recovery action failed: {action}; reboot guard retained"
            )


if __name__ == "__main__":
    main()
