#!/usr/bin/env python3
"""Local, Docker-only MCP preparation and stdio launch (host stdlib only)."""

import argparse
import contextlib
import datetime
import fcntl
import hashlib
import json
import os
import platform
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path

STACKS = {
    "u22": ("joshua:u22-humble", "humble", "3.10"),
    "u24": ("joshua:u24-jazzy", "jazzy", "3.12"),
}
BINARIES = (
    "tools/mcp/config_validation_server",
    "tools/mcp/setup_smoke",
    "config/joshua_validate_config",
    "config/joshua_inspect_config",
)
ROOT_LABEL = "io.joshua.mcp.root"


class McpError(RuntimeError):
    def __init__(self, message, code="runtime_error"):
        super().__init__(message)
        self.code = code


def storage_permission_error(path):
    blocked = Path(path)
    while blocked != blocked.parent:
        try:
            blocked.stat()
            break
        except (FileNotFoundError, PermissionError):
            blocked = blocked.parent
    quoted = shlex.quote(str(blocked))
    return McpError(
        f"MCP storage is not writable: {blocked}. Existing Docker builds may "
        "have created it as another user. If this checkout's storage should "
        "belong to you, ask its owner/administrator to run "
        f"`sudo chown {os.getuid()}:{os.getgid()} -- {quoted}`, then "
        f"`chmod u+rwX -- {quoted}` and retry. No permissions were changed.",
        "storage_unwritable",
    )


def writable_directory(path):
    try:
        path.mkdir(parents=True, exist_ok=True)
        # Check real filesystem permissions, including ACLs, before Docker work.
        with tempfile.TemporaryFile(dir=path):
            pass
    except PermissionError as error:
        raise storage_permission_error(path) from error


@contextlib.contextmanager
def container_interrupts():
    """Unwind ordinary CLI termination, deferring repeated signals in cleanup."""
    if threading.current_thread() is not threading.main_thread():
        yield lambda: None
        return
    previous = {}
    pending = None
    cleaning = False

    def begin_cleanup():
        nonlocal cleaning
        cleaning = True

    def interrupted(signum, _frame):
        nonlocal pending, cleaning
        if pending is None:
            pending = signum
        if not cleaning:
            cleaning = True
            raise SystemExit(128 + pending)

    try:
        for signum in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
            previous[signum] = signal.signal(signum, interrupted)
        yield begin_cleanup
    finally:
        for signum, handler in previous.items():
            signal.signal(signum, handler)
        if pending is not None:
            raise SystemExit(128 + pending)


def supervised_run(args, timeout=None, check=False, **options):
    """Reap the Docker CLI on every exceptional exit, including SystemExit."""
    child = None
    try:
        child = subprocess.Popen(args, start_new_session=True, **options)
        stdout, stderr = child.communicate(timeout=timeout)
        return subprocess.CompletedProcess(args, child.returncode, stdout, stderr)
    except BaseException:
        if child is not None:
            # The daemon container has an independent lifecycle. The caller's
            # finally removes its exact name after this CLI process is reaped.
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            child.communicate(timeout=2)
        raise


def host_status(system=None, machine=None, release=None):
    system = platform.system() if system is None else system
    machine = platform.machine() if machine is None else machine
    if release is None:
        release = {}
        try:
            for line in Path("/etc/os-release").read_text().splitlines():
                key, _, value = line.partition("=")
                release[key] = value.strip("\"'")
        except OSError:
            pass
    detail = " ".join(
        (system, machine, release.get("ID", ""), release.get("VERSION_ID", ""))
    ).strip()
    if system != "Linux" or machine != "x86_64" or release.get("ID") != "ubuntu":
        return {"status": "unsupported", "detail": detail}
    status = (
        "supported"
        if release.get("VERSION_ID") in ("22.04", "24.04")
        else "unqualified"
    )
    return {"status": status, "detail": detail}


def require_host():
    result = host_status()
    if result["status"] != "supported":
        raise McpError(
            f"{result['status']} host: {result['detail']}; "
            "initial local MCP targets are Ubuntu 22.04/24.04 x86_64",
            result["status"] + "_host",
        )


def host_boot_id():
    try:
        return Path("/proc/sys/kernel/random/boot_id").read_text().strip()
    except OSError as error:
        raise McpError(
            "Cannot establish this host's Linux boot identity", "same_host_unverified"
        ) from error


def owner_token(pid):
    """Linux boot/process identity, including start time to avoid PID reuse."""
    try:
        boot = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return f"{boot}:{pid}:{fields[19]}"
    except (OSError, IndexError):
        return None


def source_manifest(root):
    """Hash tracked and nonignored source, including unstaged/untracked content."""
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True
    ).strip()
    paths = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=root,
    ).split(b"\0")
    digest = hashlib.sha256()
    for name in sorted(set(paths)):
        if not name:
            continue
        path = root / os.fsdecode(name)
        digest.update(name + b"\0")
        if path.is_symlink():
            digest.update(b"link\0" + os.fsencode(os.readlink(path)))
        elif path.is_file():
            digest.update(str(path.stat().st_mode & 0o111).encode() + b"\0")
            with path.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
        else:
            digest.update(b"missing")
        digest.update(b"\0")
    return {"revision": revision, "tree_sha256": digest.hexdigest()}


def file_hashes(directory):
    result = {}
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise McpError(
                f"Prepared artifact contains a symlink: {path}", "artifacts_invalid"
            )
        if path.is_file() and path != directory / "generation.json":
            result[str(path.relative_to(directory))] = hashlib.sha256(
                path.read_bytes()
            ).hexdigest()
        elif not path.is_dir() and path != directory / "generation.json":
            raise McpError(
                f"Prepared artifact is not a regular file: {path}", "artifacts_invalid"
            )
    return result


def atomic_json(path, value):
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        with temporary.open("x") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    finally:
        temporary.unlink(missing_ok=True)


def make_readonly(directory):
    for path in directory.rglob("*"):
        path.chmod(path.stat().st_mode & ~0o222)
    directory.chmod(0o555)


def remove_staging(directory):
    if directory.exists():
        directory.chmod(0o755)
        for path in directory.rglob("*"):
            if path.is_dir():
                path.chmod(0o755)
        shutil.rmtree(directory)


class Runtime:
    def __init__(self, root, stack="u22", config_root=None, runner=None, environ=None):
        if stack not in STACKS:
            raise McpError(f"Unknown stack: {stack}", "invalid_stack")
        self.root = Path(root).resolve()
        self.stack = stack
        self.config_root = Path(
            config_root or self.root / "config/config_preset"
        ).resolve()
        self.state_dir = self.root / "dist/mcp" / stack
        self.cache_dir = self.root / ".cache/mcp" / stack
        self.runner = runner or subprocess.run
        self.environ = dict(os.environ if environ is None else environ)
        self.root_id = hashlib.sha256(os.fsencode(str(self.root))).hexdigest()[:16]

    def run(
        self, args, capture=True, check=True, env=None, timeout=30, supervise=False
    ):
        runner = (
            supervised_run
            if supervise and self.runner is subprocess.run
            else self.runner
        )
        try:
            result = runner(
                args,
                cwd=self.root,
                env=self.clean_env() if env is None else env,
                text=True,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE if capture else sys.stderr,
                stderr=subprocess.PIPE if capture else sys.stderr,
                timeout=timeout,
                check=False,
            )
        except FileNotFoundError as error:
            raise McpError(
                f"Required command missing: {args[0]}",
                "docker_missing" if args[0] == "docker" else "tool_missing",
            ) from error
        except subprocess.TimeoutExpired as error:
            raise McpError(
                f"Command timed out: {' '.join(args[:4])}", "timeout"
            ) from error
        if check and result.returncode:
            detail = (result.stderr or "").strip()[-4000:]
            raise McpError(
                f"Command failed ({result.returncode}): {' '.join(args[:6])}\n{detail}",
                "docker_unavailable",
            )
        return result

    def clean_env(self):
        env = self.environ.copy()
        for name in (
            "DOCKER_CONTEXT",
            "DOCKER_HOST",
            "DOCKER_TLS",
            "DOCKER_TLS_VERIFY",
            "DOCKER_CERT_PATH",
            "COMPOSE_FILE",
            "COMPOSE_PROFILES",
            "COMPOSE_PROJECT_NAME",
            "COMPOSE_ENV_FILES",
        ):
            env.pop(name, None)
        return env

    def docker(self, pin, *args, **kwargs):
        return self.run(["docker", "--host", pin["endpoint"], *args], **kwargs)

    def resolve_docker(self):
        context = self.environ.get("DOCKER_CONTEXT")
        endpoint = self.environ.get("DOCKER_HOST") if not context else None
        if not endpoint:
            args = ["docker", "context", "inspect"] + ([context] if context else [])
            try:
                data = json.loads(self.run(args).stdout)
                endpoint = data[0]["Endpoints"]["docker"]["Host"]
            except (ValueError, KeyError, IndexError, TypeError) as error:
                raise McpError(
                    "Cannot resolve Docker context endpoint", "docker_unavailable"
                ) from error
        if not endpoint.startswith("unix:///") or "\0" in endpoint:
            raise McpError(
                f"Only a local unix Docker endpoint is supported; got {endpoint}",
                "remote_docker",
            )
        endpoint = "unix://" + str(Path(endpoint[7:]).resolve())
        pin = {"endpoint": endpoint}
        info = self.daemon_info(pin)
        pin["daemon_id"] = info["ID"]
        return pin

    def daemon_info(self, pin, timeout=30):
        try:
            info = json.loads(
                self.docker(
                    pin, "info", "--format", "{{json .}}", timeout=timeout
                ).stdout
            )
        except (ValueError, TypeError) as error:
            raise McpError(
                "Docker daemon did not return usable identity", "docker_unavailable"
            ) from error
        if (
            not info.get("ID")
            or info.get("OSType") != "linux"
            or info.get("Architecture") not in ("x86_64", "amd64")
        ):
            raise McpError(
                "Local MCP requires a Linux x86_64 Docker daemon with an identity",
                "unsupported_daemon",
            )
        if pin.get("daemon_id") and info["ID"] != pin["daemon_id"]:
            raise McpError(
                "Pinned Docker daemon identity changed; run prepare again",
                "daemon_changed",
            )
        return info

    def compose_base(self, pin, compose_file=None):
        return [
            "docker",
            "--host",
            pin["endpoint"],
            "compose",
            "--project-name",
            "joshua-mcp-" + self.root_id,
            "--project-directory",
            str(self.root),
            "--env-file",
            "/dev/null",
            "-f",
            str(compose_file or self.root / "docker-compose.mcp.yml"),
        ]

    def compose_env(self, state, artifact_dir=None):
        env = self.clean_env()
        env.update(
            {
                "JOSHUA_MCP_ROOT": str(self.root),
                "JOSHUA_MCP_ROOT_ID": self.root_id,
                "JOSHUA_MCP_STACK": self.stack,
                "JOSHUA_MCP_IMAGE": state["image_id"],
                "JOSHUA_MCP_ROS_DISTRO": STACKS[self.stack][1],
                "JOSHUA_MCP_CONFIG_ROOT": str(self.config_root),
                "JOSHUA_MCP_ARTIFACTS": str(
                    artifact_dir or self.state_dir / state["generation"]
                ),
                "JOSHUA_MCP_USER": f"{os.getuid()}:{os.getgid()}",
                "JOSHUA_MCP_OWNER": owner_token(os.getpid()) or "unknown",
                "JOSHUA_MCP_CACHE": str(self.cache_dir),
            }
        )
        return env

    def compose_command(self, state, service, extra=(), artifact_dir=None):
        name = f"joshua-mcp-{self.root_id}-{self.stack}-{uuid.uuid4().hex[:12]}"
        definition = (
            None
            if service == "mcp-prepare"
            else Path(artifact_dir or self.state_dir / state["generation"])
            / "docker-compose.mcp.yml"
        )
        command = self.compose_base(state["docker"], definition) + [
            "run",
            "--rm",
            "--no-deps",
            "-T",
            "--name",
            name,
            service,
            *extra,
        ]
        return command, self.compose_env(state, artifact_dir)

    def cleanup_container(self, state, name):
        try:
            self.daemon_info(state["docker"], timeout=5)
            self.docker(state["docker"], "rm", "--force", name, check=False, timeout=5)
        except McpError as error:
            print(f"MCP cleanup: {error}; inspect container {name}", file=sys.stderr)

    def run_container(
        self, state, service, extra=(), artifact_dir=None, capture=True, timeout=45
    ):
        command, env = self.compose_command(state, service, extra, artifact_dir)
        name = command[command.index("--name") + 1]
        with container_interrupts() as begin_cleanup:
            try:
                return self.run(
                    command, capture=capture, env=env, timeout=timeout, supervise=True
                )
            finally:
                # Ordinary signals unwind here before the preparation lock is
                # released. SIGKILL cannot run finally; doctor reports leftovers.
                begin_cleanup()
                self.cleanup_container(state, name)

    def require_config_root(self):
        if not self.config_root.is_dir():
            raise McpError(
                f"Config root is not a directory: {self.config_root}",
                "invalid_config_root",
            )

    def probe_docker(self, state, artifact_dir=None):
        boot = host_boot_id()
        nonce = uuid.uuid4().hex
        path = self.state_dir / (".mount-probe-" + nonce)
        path.write_text(nonce)
        path.chmod(0o644)
        code = (
            'printf "%s\\n%s\\n" "$(cat /proc/sys/kernel/random/boot_id)" "$(cat "$1")"'
        )
        try:
            result = self.run_container(
                state,
                "mcp-probe",
                [
                    "/bin/sh",
                    "-c",
                    code,
                    "mcp-probe",
                    "/workspace/" + str(path.relative_to(self.root)),
                ],
                artifact_dir=artifact_dir,
                timeout=15,
            )
            try:
                probe = result.stdout.splitlines()
                if probe != [boot, nonce]:
                    raise ValueError("host boot or bind mount mismatch")
            except (ValueError, TypeError) as error:
                raise McpError(
                    "Docker host boot or checkout mount differs; "
                    "remote/VM daemons are unsupported",
                    "same_host_unverified",
                ) from error
        finally:
            path.unlink(missing_ok=True)

    def require_compose(self, pin):
        result = self.docker(pin, "compose", "version", check=False)
        if result.returncode:
            raise McpError(
                "Docker Compose v2 is missing; follow the host setup instructions",
                "compose_missing",
            )

    def image_info(self, pin, name, required=True):
        result = self.docker(pin, "image", "inspect", name, check=False)
        if result.returncode:
            if required:
                raise McpError(
                    f"Prepared image {name} is missing; run prepare", "image_missing"
                )
            return None
        try:
            info = json.loads(result.stdout)[0]
            if (
                info["Os"] != "linux"
                or info["Architecture"] != "amd64"
                or not info["Id"].startswith("sha256:")
            ):
                raise ValueError("image platform or ID")
            return info["Id"]
        except (ValueError, KeyError, IndexError, TypeError) as error:
            raise McpError(
                "Docker image is not a Linux amd64 image with an immutable ID",
                "image_invalid",
            ) from error

    @contextlib.contextmanager
    def prepare_lock(self):
        writable_directory(self.state_dir)
        path = self.state_dir / "prepare.lock"
        try:
            stream = path.open("a")
        except PermissionError as error:
            raise storage_permission_error(path) from error
        with stream:
            try:
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise McpError(
                    "Another prepare is already running for this checkout and stack",
                    "prepare_locked",
                ) from error
            try:
                yield
            finally:
                fcntl.flock(stream, fcntl.LOCK_UN)

    def prepare(self, rebuild_image=False):
        require_host()
        self.require_config_root()
        started = time.monotonic()
        with self.prepare_lock():
            writable_directory(self.cache_dir)
            pin = self.resolve_docker()
            self.require_compose(pin)
            source = source_manifest(self.root)
            image = self.image_info(pin, STACKS[self.stack][0], required=False)
            need_build = image is None or rebuild_image
            if image is None:
                # Prove the daemon/mount before sending any project build context.
                # Both the bootstrap image and the project images have sh and cat.
                image = self.image_info(pin, "ubuntu:22.04", required=False)
                if image is None:
                    with container_interrupts():
                        self.docker(
                            pin,
                            "pull",
                            "--platform",
                            "linux/amd64",
                            "ubuntu:22.04",
                            capture=False,
                            timeout=None,
                            supervise=True,
                        )
                    image = self.image_info(pin, "ubuntu:22.04")
            generations = self.state_dir / "generations"
            writable_directory(generations)
            identifier = uuid.uuid4().hex
            staging = generations / (".preparing-" + identifier)
            staging.mkdir()
            shutil.copyfile(
                self.root / "docker-compose.mcp.yml", staging / "docker-compose.mcp.yml"
            )
            state = {
                "version": 1,
                "stack": self.stack,
                "root": str(self.root),
                "config_root": str(self.config_root),
                "generation": "generations/" + identifier,
                "image_id": image,
                "docker": pin,
                "host_boot_id": host_boot_id(),
                "source": source,
            }
            try:
                self.daemon_info(pin)
                self.probe_docker(state, artifact_dir=staging)
                if need_build:
                    print(
                        f"Building {STACKS[self.stack][0]} for local MCP",
                        file=sys.stderr,
                    )
                    with container_interrupts():
                        self.run(
                            self.compose_base(pin)
                            + ["build", "mcp-build-" + self.stack],
                            capture=False,
                            env=self.compose_env(state, staging),
                            timeout=None,
                            supervise=True,
                        )
                    state["image_id"] = self.image_info(pin, STACKS[self.stack][0])
                    self.daemon_info(pin)
                print(
                    f"Preparing local MCP ({self.stack}); build output is on stderr",
                    file=sys.stderr,
                )
                self.run_container(
                    state,
                    "mcp-prepare",
                    artifact_dir=staging,
                    capture=False,
                    timeout=None,
                )
                self.daemon_info(pin)
                for name in BINARIES:
                    if not (staging / name).is_file():
                        raise McpError(
                            f"Missing prepared artifact: {name}", "artifacts_invalid"
                        )
                hashes = file_hashes(staging)
                smoke = self.run_container(state, "mcp-check", artifact_dir=staging)
                try:
                    report = json.loads(smoke.stdout)
                    if report.get("ok") is not True:
                        raise ValueError("smoke failed")
                except (ValueError, TypeError) as error:
                    raise McpError(
                        "Prepared MCP protocol smoke did not pass", "smoke_failed"
                    ) from error
                if source_manifest(self.root) != source:
                    raise McpError(
                        "Source changed during prepare; retry after edits finish. "
                        "Previous generation preserved.",
                        "source_changed",
                    )
                state.update(
                    {
                        "prepared_at": datetime.datetime.now(
                            datetime.timezone.utc
                        ).isoformat(),
                        "prepare_seconds": round(time.monotonic() - started, 3),
                        "smoke": report,
                    }
                )
                atomic_json(
                    staging / "generation.json", {"state": state, "files": hashes}
                )
                make_readonly(staging)
                final = self.state_dir / state["generation"]
                staging.rename(final)
                atomic_json(self.state_dir / "active.json", state)
                return state
            finally:
                remove_staging(staging)

    def read_state(self):
        path = self.state_dir / "active.json"
        try:
            state = json.loads(path.read_text())
        except FileNotFoundError as error:
            raise McpError(
                "MCP is not prepared; run scripts/joshua_mcp.sh prepare", "unprepared"
            ) from error
        except (OSError, ValueError) as error:
            raise McpError(
                f"Cannot read prepared state: {error}", "state_invalid"
            ) from error
        try:
            for key in (
                "stack",
                "root",
                "config_root",
                "generation",
                "image_id",
                "host_boot_id",
            ):
                if not isinstance(state[key], str) or not state[key]:
                    raise ValueError(key)
            for key in ("endpoint", "daemon_id"):
                if (
                    not isinstance(state["docker"][key], str)
                    or not state["docker"][key]
                ):
                    raise ValueError(key)
            for key in ("revision", "tree_sha256"):
                if (
                    not isinstance(state["source"][key], str)
                    or not state["source"][key]
                ):
                    raise ValueError(key)
            if state["version"] != 1 or state["stack"] != self.stack:
                raise ValueError("version/stack")
            if state["root"] != str(self.root) or state["config_root"] != str(
                self.config_root
            ):
                raise McpError(
                    "Prepared checkout root or config-root differs; run prepare again",
                    "state_invalid",
                )
            generation = Path(state["generation"])
            if (
                len(generation.parts) != 2
                or generation.parts[0] != "generations"
                or len(generation.parts[1]) != 32
                or any(c not in "0123456789abcdef" for c in generation.parts[1])
            ):
                raise ValueError("generation")
            if (
                not state["docker"]["endpoint"].startswith("unix:///")
                or not state["docker"]["daemon_id"]
                or not state["image_id"].startswith("sha256:")
            ):
                raise ValueError("pin")
        except (KeyError, TypeError, ValueError) as error:
            raise McpError(
                "Invalid prepared state; run prepare", "state_invalid"
            ) from error
        return state

    def checked_state(self):
        self.require_config_root()
        state = self.read_state()
        self.daemon_info(state["docker"])
        self.require_compose(state["docker"])
        self.image_info(state["docker"], state["image_id"])
        generation = self.state_dir / state["generation"]
        try:
            manifest = json.loads((generation / "generation.json").read_text())
            if manifest["state"] != state or manifest["files"] != file_hashes(
                generation
            ):
                raise ValueError("hash mismatch")
            for name in BINARIES:
                if not (generation / name).is_file():
                    raise ValueError("missing binary")
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise McpError(
                "Prepared artifacts failed integrity validation; run prepare",
                "artifacts_invalid",
            ) from error
        self.probe_docker(state)
        return state

    def containers(self, pin):
        result = self.docker(
            pin,
            "ps",
            "--filter",
            f"label={ROOT_LABEL}={self.root_id}",
            "--filter",
            f"label=io.joshua.mcp.stack={self.stack}",
            "--format",
            "{{json .}}",
        )
        sessions = [
            json.loads(line) for line in result.stdout.splitlines() if line.strip()
        ]
        for entry in sessions:
            labels = dict(
                item.split("=", 1)
                for item in entry.get("Labels", "").split(",")
                if "=" in item
            )
            token = labels.get("io.joshua.mcp.owner", "")
            try:
                pid = int(token.split(":")[1])
                entry["owner_alive"] = owner_token(pid) == token
            except (ValueError, IndexError):
                entry["owner_alive"] = None
        return sessions

    def doctor(self):
        host = host_status()
        report = {
            "ok": False,
            "status": "unprepared",
            "stack": self.stack,
            "root": str(self.root),
            "config_root": str(self.config_root),
            "host": host,
            "prepared": False,
            "source_current": False,
            "problems": [],
            "problem_codes": [],
            "orphan_containers": [],
        }
        if host["status"] != "supported":
            report["problems"].append(f"{host['status']} host: {host['detail']}")
            report["problem_codes"].append(host["status"] + "_host")
        try:
            try:
                saved = self.read_state()
            except McpError as error:
                if error.code != "unprepared":
                    raise
                pin = self.resolve_docker()
            else:
                pin = saved["docker"]
                self.daemon_info(pin)
            self.require_compose(pin)
            report["docker"] = pin
            sessions = self.containers(pin)
            report["orphan_containers"] = [
                entry for entry in sessions if entry["owner_alive"] is not True
            ]
            report["active_containers"] = [
                entry for entry in sessions if entry["owner_alive"] is True
            ]
            try:
                state = self.checked_state()
            except McpError as error:
                if error.code == "unprepared":
                    report["problems"].append(str(error))
                    report["problem_codes"].append(error.code)
                else:
                    raise
            else:
                report["state"] = state
                report["prepared"] = True
                report["source_current"] = state["source"] == source_manifest(self.root)
                report["status"] = "ready" if report["source_current"] else "stale"
                if not report["source_current"]:
                    report["problems"].append(
                        "Source changed since prepare; serve uses the saved generation."
                        " "
                        "Run prepare to update it."
                    )
                    report["problem_codes"].append("source_changed")
        except McpError as error:
            report["status"] = "error"
            report["problems"].append(str(error))
            report["problem_codes"].append(error.code)
        except (OSError, subprocess.SubprocessError, ValueError) as error:
            report["status"] = "error"
            report["problems"].append(str(error))
            report["problem_codes"].append("inspection_failed")
        if host["status"] != "supported":
            report["status"] = "error"
        report["ok"] = report["status"] == "ready" and not report["problems"]
        return report

    def check(self, config=None):
        require_host()
        state = self.checked_state()
        args = ["/opt/joshua-mcp/tools/mcp/setup_smoke"]
        if config is not None:
            args += ["--config", config]
        result = self.run_container(state, "mcp-check", args)
        try:
            report = json.loads(result.stdout)
            if report.get("ok") is not True:
                raise ValueError("MCP smoke failed")
            return report
        except (ValueError, TypeError) as error:
            raise McpError(
                "MCP protocol check did not produce a passing report", "smoke_failed"
            ) from error

    def serve(self):
        require_host()
        state = self.checked_state()
        if state["source"] != source_manifest(self.root):
            print(
                "Joshua MCP source changed; serving the prepared generation. "
                "Run prepare to update.",
                file=sys.stderr,
            )
        command, env = self.compose_command(state, "mcp-serve")
        name = command[command.index("--name") + 1]
        child = None
        previous = {}
        interrupted = None

        def stop(signum, _frame):
            nonlocal interrupted
            interrupted = signum
            # Scoped name and daemon pin avoid affecting another launch. SIGKILL
            # cannot run this handler; doctor lists surviving labeled sessions.
            try:
                self.daemon_info(state["docker"])
                self.docker(
                    state["docker"], "stop", "--time", "2", name, check=False, timeout=5
                )
            except McpError as error:
                print(
                    f"MCP cleanup: {error}; inspect docker container {name}",
                    file=sys.stderr,
                )
            if child is not None and child.poll() is None:
                child.send_signal(signum)

        try:
            for signum in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
                previous[signum] = signal.signal(signum, stop)
            if interrupted is not None:
                return 128 + interrupted
            child = subprocess.Popen(command, cwd=self.root, env=env)
            if interrupted is not None:
                stop(interrupted, None)
            return child.wait()
        finally:
            for signum, handler in previous.items():
                signal.signal(signum, handler)
            self.cleanup_container(state, name)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "setup":
        try:
            from mcp_setup import main as setup_main
        except ImportError as error:
            raise McpError(
                "Guided setup is not in this revision; use prepare/doctor/check",
                "setup_unavailable",
            ) from error
        return setup_main(argv[1:])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "serve", "doctor", "check"))
    parser.add_argument("--stack", choices=STACKS, default="u22")
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--config-root", type=Path)
    parser.add_argument("--config", help="Relative preset to validate during check")
    parser.add_argument(
        "--rebuild-image",
        action="store_true",
        help="Explicitly rebuild the stack image during prepare",
    )
    args = parser.parse_args(argv)
    runtime = Runtime(args.root, args.stack, args.config_root)
    if args.command == "serve":
        return runtime.serve()
    if args.command == "prepare":
        result = runtime.prepare(rebuild_image=args.rebuild_image)
    elif args.command == "check":
        result = runtime.check(args.config)
    else:
        result = runtime.doctor()
        from mcp_setup import registration_status

        try:
            registrations = registration_status(runtime.root, runtime.stack)
        except (OSError, RuntimeError, ValueError) as error:
            registrations = {"client": {"status": "attention", "detail": str(error)}}
        result["registrations"] = registrations
        if any(
            isinstance(value, dict) and value.get("status") == "attention"
            for value in registrations.values()
        ):
            result["ok"] = False
            result["status"] = "error"
            result["problems"].append("AI IDE registration needs attention; run setup.")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if args.command == "prepare" or result.get("ok") else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (McpError, OSError, subprocess.SubprocessError) as error:
        print(f"Joshua MCP: {error}", file=sys.stderr)
        raise SystemExit(1)
