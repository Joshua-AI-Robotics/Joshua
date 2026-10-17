"""Render a new private Home Assistant deployment from a Joshua protobuf preset."""

import argparse
import ipaddress
import json
import os
import re
import secrets
from datetime import datetime, timezone
from pathlib import Path

from config.proto.config_pb2 import Config
from google.protobuf import text_format


def render(config, output, tailscale_ip):
    address = ipaddress.IPv4Address(tailscale_ip)
    if address not in ipaddress.ip_network("100.64.0.0/10"):
        raise ValueError("Expected the Pi's Tailscale IPv4 address")
    streams = list(config.utilities.media_streams)
    if len(streams) != 1:
        raise ValueError("This deployment requires exactly one media stream")
    stream = streams[0]
    if (
        not re.fullmatch(r"[A-Za-z0-9_-]+", stream.stream_name)
        or not 1024 <= stream.rtsp_port <= 65535
    ):
        raise ValueError("Invalid media stream endpoint")
    if {stream.rtsp_port, stream.http_port, stream.ice_port} & {
        1984,
        8123,
        18554,
        18555,
    }:
        raise ValueError("Media ports conflict with Home Assistant deployment ports")
    rtsp = f"rtsp://127.0.0.1:{stream.rtsp_port}/{stream.stream_name}"
    output = Path(output).expanduser()
    # Refuse to overwrite Home Assistant accounts, secrets, or user changes.
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    templates = Path(__file__).resolve().parents[2] / "deploy/home_assistant"
    (output / "config/packages").mkdir(parents=True)
    # HA 2026.8+ stores HTTP settings here. Seed only a NEW deployment, so
    # onboarding cannot roll an unconfirmed YAML migration back to 0.0.0.0.
    (output / "config/.storage").mkdir(mode=0o700)
    stable_http = {
        "server_host": ["127.0.0.1"],
        "server_port": 8123,
        "use_x_forwarded_for": True,
        "trusted_proxies": ["127.0.0.1/32", "::1/128"],
        "cors_allowed_origins": [],
        "login_attempts_threshold": -1,
        "ip_ban_enabled": True,
        "ssl_profile": "modern",
        "use_x_frame_options": True,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "error": None,
        "error_message": None,
    }
    (output / "config/.storage/http").write_text(
        json.dumps(
            {
                "version": 2,
                "minor_version": 2,
                "key": "http",
                "data": {
                    "stable": stable_http,
                    "pending": None,
                    "yaml_migration_done": True,
                },
            },
            indent=2,
        )
        + "\n"
    )
    for name in ("compose.yaml", "README.md"):
        data = (templates / name).read_text().replace("RTSP_URL_PLACEHOLDER", rtsp)
        (output / name).write_text(data)
    for name in ("configuration.yaml", "baby-monitor.yaml"):
        (output / "config" / name).write_text((templates / name).read_text())
    package = templates / "joshua.yaml"
    if package.exists():
        (output / "config/packages/joshua.yaml").write_text(package.read_text())
    secret = secrets.token_hex(32)
    for path, content in (
        (output / "webhook.secret", secret + "\n"),
        (output / "config/secrets.yaml", "joshua_webhook: " + secret + "\n"),
    ):
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as file:
            file.write(content)
    # JSON is valid YAML. Restrict unauthenticated control APIs to loopback.
    (output / "go2rtc.yaml").write_text(
        json.dumps(
            {
                "api": {"listen": "127.0.0.1:1984"},
                "rtsp": {"listen": "127.0.0.1:18554"},
                "webrtc": {
                    "listen": f"{address}:18555",
                    "candidates": [f"{address}:18555"],
                    "ice_servers": [],
                },
            },
            indent=2,
        )
        + "\n"
    )
    print(f"Deployment created: {output}")
    print(f"Generic Camera stream source: {rtsp}")
    print("Follow README.md to start the server and connect the phones.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--tailscale-ip", required=True)
    args = parser.parse_args()
    config = text_format.Parse(Path(args.config).read_text(), Config())
    render(config, args.output, args.tailscale_ip)


if __name__ == "__main__":
    main()
