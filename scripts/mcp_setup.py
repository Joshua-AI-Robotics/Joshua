"""Guided, resumable onboarding using native MCP client configuration commands."""

import argparse
import fcntl
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


class SetupError(RuntimeError):
    pass


def server_entry(root, stack, config_root):
    return {
        "command": str(root / "scripts/joshua_mcp.sh"),
        "args": [
            "serve",
            "--stack",
            stack,
            "--root",
            str(root),
            "--config-root",
            str(config_root),
        ],
    }


def same_server(actual, expected):
    return (
        isinstance(actual, dict)
        and actual.get("command") == expected["command"]
        and actual.get("args", []) == expected["args"]
        and actual.get("type", "stdio") == "stdio"
        and actual.get("enabled", True) is not False
        and not actual.get("env")
        and not actual.get("env_vars")
    )


def read_json(path):
    try:
        value = json.loads(path.read_text()) if path.exists() else {}
        if not isinstance(value, dict):
            raise ValueError("expected an object")
        return value
    except (OSError, ValueError) as exc:
        raise SetupError(f"Cannot read client configuration {path}: {exc}") from exc


def json_client_path():
    directory = os.environ.get("CLAUDE_CONFIG_DIR")
    return (
        Path(directory).expanduser() / ".claude.json"
        if directory
        else Path.home() / ".claude.json"
    )


def run_client(argv, cwd):
    try:
        result = subprocess.run(
            argv,
            cwd=cwd,
            env={**os.environ, "PWD": str(cwd)},
            capture_output=True,
            text=True,
            timeout=45,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise SetupError(f"Client configuration command failed: {exc}") from exc
    if result.returncode:
        # Configuration output may contain unrelated credentials. Do not print it.
        raise SetupError(
            "Client configuration command failed; inspect its MCP settings and retry "
            "setup. Existing entries were not replaced."
        )
    return result.stdout


def cli_servers(cwd):
    try:
        entries = json.loads(run_client(["codex", "mcp", "list", "--json"], cwd))
        if not isinstance(entries, list) or any(
            not isinstance(e, dict) for e in entries
        ):
            raise ValueError("expected server list")
        return {e["name"]: e for e in entries}
    except (ValueError, KeyError) as exc:
        raise SetupError(
            "Cannot interpret the client MCP configuration; use --client manual."
        ) from exc


def client_entry(client, cwd):
    if client == "codex":
        item = cli_servers(cwd).get("joshua")
        if item is None:
            return None
        entry = dict(item.get("transport", {}))
        entry["enabled"] = item.get("enabled", True)
        return entry
    data = read_json(json_client_path())
    entries = data.get("mcpServers", {})
    if not isinstance(entries, dict):
        raise SetupError("Client mcpServers configuration must be an object.")
    return entries.get("joshua")


def check_project_shadow(client, root, expected):
    if client == "codex":
        effective = client_entry(client, root)
        if effective is not None and not same_server(effective, expected):
            raise SetupError(
                "A project or managed MCP configuration shadows/conflicts with Joshua. "
                "Resolve it in the AI IDE, then retry."
            )
        return
    data = read_json(json_client_path())
    projects = data.get("projects", {})
    if not isinstance(projects, dict):
        raise SetupError("Cannot interpret client project configuration.")
    project = projects.get(str(root), {})
    for value in (read_json(root / ".mcp.json"), project):
        if not isinstance(value, dict):
            raise SetupError("Cannot interpret project MCP configuration.")
        servers = value.get("mcpServers", {})
        if not isinstance(servers, dict):
            raise SetupError("Cannot interpret project MCP configuration.")
        entry = servers.get("joshua")
        if entry is not None and not same_server(entry, expected):
            raise SetupError(
                "A project MCP configuration shadows Joshua. "
                "Resolve it in the AI IDE, then retry."
            )


def user_config_cwd(client):
    if client != "codex":
        return Path.home()
    # HOME can itself be a trusted project with a separate custom user profile.
    # On our Linux hosts, / has no parent project layers. Refuse ambiguity there.
    directory = Path("/")
    if (directory / ".codex/config.toml").exists():
        raise SetupError(
            "Root project settings prevent isolated user-registration readback; "
            "use --client manual."
        )
    return directory


def register(client, entry, root):
    check_project_shadow(client, root, entry)
    user_cwd = user_config_cwd(client)
    existing = client_entry(client, user_cwd)
    if existing is not None:
        if not same_server(existing, entry):
            raise SetupError(
                "Existing Joshua MCP registration conflicts with this checkout or "
                "is disabled. Resolve the conflict in the AI IDE, or keep using that "
                "checkout; setup will not overwrite it."
            )
        return "existing"
    if client == "codex":
        run_client(
            ["codex", "mcp", "add", "joshua", "--", entry["command"], *entry["args"]],
            user_cwd,
        )
    elif client == "claude":
        run_client(
            [
                "claude",
                "mcp",
                "add-json",
                "--scope",
                "user",
                "joshua",
                json.dumps({"type": "stdio", **entry}),
            ],
            user_cwd,
        )
    else:
        raise SetupError("Unsupported client adapter.")
    if not same_server(client_entry(client, user_cwd), entry):
        raise SetupError(
            "Registration readback did not match; "
            "inspect the AI IDE MCP settings before retrying."
        )
    check_project_shadow(client, root, entry)
    return "added"


def choose_client(requested):
    installed = [name for name in ("codex", "claude") if shutil.which(name)]
    if requested != "auto":
        if requested != "manual" and requested not in installed:
            raise SetupError(
                f"The requested client CLI ({requested}) is not installed. "
                "Use --client manual for generic stdio settings."
            )
        return requested
    if len(installed) < 2:
        return installed[0] if installed else "manual"
    if not sys.stdin.isatty():
        raise SetupError(
            "Multiple AI client CLIs detected; rerun setup with --client "
            + " or --client ".join(installed)
            + "."
        )
    print("Choose the AI IDE configuration to connect:", file=sys.stderr)
    for index, name in enumerate(installed, 1):
        print(f"  {index}. {name}", file=sys.stderr)
    answer = input("Selection [1]: ").strip() or "1"
    if answer not in ("1", "2"):
        raise SetupError("Choose one of the detected clients or use --client manual.")
    return installed[int(answer) - 1]


def offer_bootstrap(root, report):
    if report.get("host", {}).get("status") in ("unsupported", "unqualified"):
        return
    codes = set(report.get("problem_codes", []))
    if not codes.intersection({"docker_missing", "compose_missing"}):
        return
    print(
        "Docker tooling is missing. The existing host bootstrap installs Docker "
        "Engine, Compose and buildx; it can REMOVE conflicting docker.io/containerd/"
        "runc packages, enable the daemon, add docker/PTP groups and install a PTP "
        "udev rule. It does not install host ROS, Bazel or GPU tooling in CPU mode. "
        "A new login may be required.",
        file=sys.stderr,
    )
    print(f"Command: sudo {root / 'scripts/setup.sh'} --cpu", file=sys.stderr)
    if not sys.stdin.isatty() or input(
        "Run these host changes now? [y/N]: "
    ).strip().lower() not in ("y", "yes"):
        raise SetupError(
            "Host setup is pending. Run the displayed command when ready, "
            "then rerun this setup command."
        )
    result = subprocess.run(
        ["sudo", str(root / "scripts/setup.sh"), "--cpu"], cwd=root, check=False
    )
    if result.returncode:
        raise SetupError(
            "Host bootstrap did not finish. "
            "Resolve its reported problem and rerun setup."
        )


def registration_status(root, stack):
    """Read registration state; a saved entry does not prove client approval."""
    record = read_json(root / "dist/mcp" / stack / "registration.json")
    if not record:
        return {
            "status": "not_registered",
            "detail": "Run setup or configure the generic stdio entry in your AI IDE.",
        }
    statuses = {}
    clients = record.get("clients", {})
    if not isinstance(clients, dict):
        raise SetupError("Cannot interpret saved registration metadata; rerun setup.")
    for client, entry in clients.items():
        if client not in ("codex", "claude"):
            continue
        try:
            check_project_shadow(client, root, entry)
            if not same_server(client_entry(client, user_config_cwd(client)), entry):
                raise SetupError("Saved registration has changed or is disabled.")
            statuses[client] = {
                "status": "saved",
                "detail": "Reconnect the AI IDE; its approvals and policies apply.",
            }
        except (SetupError, OSError, KeyError) as exc:
            statuses[client] = {"status": "attention", "detail": str(exc)}
    return statuses


def record_registration(root, stack, client, entry):
    from mcp_runtime import atomic_json

    directory = root / "dist/mcp" / stack
    with (directory / "registration.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path = directory / "registration.json"
        data = read_json(path)
        clients = data.setdefault("clients", {})
        if not isinstance(clients, dict):
            raise SetupError("Cannot interpret saved registration metadata.")
        clients[client] = entry
        atomic_json(path, data)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Prepare local MCP and connect an AI IDE."
    )
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--stack", choices=("u22", "u24"), default="u22")
    parser.add_argument("--config-root", type=Path)
    parser.add_argument(
        "--client", choices=("auto", "codex", "claude", "manual"), default="auto"
    )
    args = parser.parse_args(argv)
    root = args.root.expanduser().resolve()
    config_root = (
        (args.config_root or root / "config/config_preset").expanduser().resolve()
    )
    try:
        from mcp_runtime import McpError, Runtime

        client = choose_client(args.client)
        runtime = Runtime(root, args.stack, config_root)
        report = runtime.doctor()
        offer_bootstrap(root, report)
        print(
            "Preparing CPU-only MCP; first use may download a large image "
            "and compile dependencies.",
            file=sys.stderr,
        )
        state = report["state"] if report.get("ok") else runtime.prepare()
        runtime.check()
        entry = server_entry(root, args.stack, config_root)
        if client == "manual":
            print(json.dumps({"mcpServers": {"joshua": entry}}, indent=2))
            print(
                "Inspection verified. Add these stdio settings in your AI IDE, "
                "then reconnect and request list_supported_components. "
                "Registration is still pending.",
                file=sys.stderr,
            )
            return 2
        registration = register(client, entry, root)
        record_registration(root, args.stack, client, entry)
        # Native client CLI owns its configuration; never edit approval settings.
        result = subprocess.run(
            [str(root / "scripts/install-skills.sh")], cwd=root, check=False
        )
        if result.returncode:
            print(
                "Skill linking needs attention; the integration guide "
                "remains available in root documentation.",
                file=sys.stderr,
            )
        print(
            json.dumps(
                {
                    "inspection": "verified",
                    "registration": registration,
                    "client": client,
                    "prepare_seconds": state.get("prepare_seconds"),
                }
            )
        )
        print(
            "MCP is prepared and registration is saved. Reconnect your AI IDE and "
            "complete any client approval. Ask it to list supported components, "
            "then guide your hardware configuration. Setup does not launch hardware.",
            file=sys.stderr,
        )
        return 0
    except (SetupError, McpError, OSError, ValueError) as exc:
        print(f"MCP setup: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
