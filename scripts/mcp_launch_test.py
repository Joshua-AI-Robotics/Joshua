#!/usr/bin/env python3
"""Host-side launcher regressions; no Docker daemon or third-party packages."""

import importlib.util
import json
import os
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent


def load(name):
    path = HERE / (name + ".py")
    if not path.exists():
        return None
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runtime = load("mcp_runtime")
packager = load("mcp_container_prepare")


class RuntimeTest(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(runtime, "The host MCP runtime has not been implemented")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "config/config_preset").mkdir(parents=True)
        (self.root / "docker-compose.mcp.yml").write_text("services: {}\n")
        self.calls = []
        self.daemon = "local-daemon-A"
        self.image = "sha256:" + "a" * 64
        self.fail_prepare = False
        self.fail_check = False
        self.fail_check_timeout = False
        self.wrong_probe_boot = False
        self.wrong_probe_nonce = False
        self.project_image_missing = False
        self.bootstrap_image_missing = False
        self.runtime = runtime.Runtime(self.root, runner=self.fake_run, environ={})

    def fake_run(self, argv, **kwargs):
        self.calls.append((argv, kwargs))
        stdout = ""
        code = 0
        if "context" in argv:
            stdout = '[{"Endpoints":{"docker":{"Host":"unix:///tmp/docker.sock"}}}]'
        elif "info" in argv:
            stdout = json.dumps(
                {"ID": self.daemon, "OSType": "linux", "Architecture": "x86_64"}
            )
        elif "inspect" in argv and "image" in argv:
            if (argv[-1] == "joshua:u22-humble" and self.project_image_missing) or (
                argv[-1] == "ubuntu:22.04" and self.bootstrap_image_missing
            ):
                return subprocess.CompletedProcess(argv, 1, "", "missing image")
            stdout = json.dumps(
                [{"Id": self.image, "Os": "linux", "Architecture": "amd64"}]
            )
        elif "build" in argv:
            self.project_image_missing = False
        elif "pull" in argv:
            self.bootstrap_image_missing = False
        elif "ps" in argv:
            stdout = ""
        elif "run" in argv and "mcp-prepare" in argv:
            if self.fail_prepare:
                code = 1
            else:
                stage = Path(kwargs["env"]["JOSHUA_MCP_ARTIFACTS"])
                for name in runtime.BINARIES:
                    target = stage / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_text("#!/bin/sh\nexit 0\n")
                    target.chmod(0o755)
        elif "mcp-check" in argv:
            if self.fail_check_timeout:
                raise subprocess.TimeoutExpired(argv, 45)
            stdout = json.dumps({"ok": not self.fail_check})
        elif "mcp-probe" in argv:
            nonce = (self.root / argv[-1].removeprefix("/workspace/")).read_text()
            boot = "wrong" if self.wrong_probe_boot else runtime.host_boot_id()
            nonce = "wrong" if self.wrong_probe_nonce else nonce
            stdout = boot + "\n" + nonce + "\n"
        elif "rev-parse" in argv:
            stdout = "abc123"
        elif "ls-files" in argv:
            stdout = ""
        return subprocess.CompletedProcess(argv, code, stdout, "")

    def prepare(self):
        with mock.patch.object(runtime, "require_host"), mock.patch.object(
            runtime,
            "source_manifest",
            return_value={"revision": "abc123", "tree_sha256": "abc"},
        ):
            return self.runtime.prepare()

    def test_host_release_distinguishes_unqualified_from_unsupported(self):
        self.assertEqual(
            runtime.host_status(
                "Linux", "x86_64", {"ID": "ubuntu", "VERSION_ID": "24.04"}
            )["status"],
            "supported",
        )
        self.assertEqual(
            runtime.host_status(
                "Linux", "x86_64", {"ID": "ubuntu", "VERSION_ID": "26.04"}
            )["status"],
            "unqualified",
        )
        self.assertEqual(
            runtime.host_status(
                "Linux", "aarch64", {"ID": "ubuntu", "VERSION_ID": "24.04"}
            )["status"],
            "unsupported",
        )
        self.assertEqual(
            runtime.host_status("Darwin", "x86_64", {})["status"], "unsupported"
        )

    def test_remote_endpoint_is_rejected_before_daemon_access(self):
        for endpoint in (
            "tcp://127.0.0.1:2375",
            "ssh://host",
            "unix://relative",
            "npipe:////./pipe/docker_engine",
        ):
            with self.subTest(endpoint=endpoint):
                with self.assertRaises(runtime.McpError):
                    runtime.Runtime(
                        self.root,
                        runner=self.fake_run,
                        environ={"DOCKER_HOST": endpoint},
                    ).resolve_docker()
        self.assertFalse(any("info" in c[0] for c in self.calls))

    def test_explicit_context_takes_precedence_and_is_resolved_once(self):
        item = runtime.Runtime(
            self.root,
            runner=self.fake_run,
            environ={"DOCKER_CONTEXT": "local", "DOCKER_HOST": "ssh://ignored"},
        )
        pin = item.resolve_docker()
        self.assertEqual(pin["endpoint"], "unix:///tmp/docker.sock")
        context_call = next(args for args, _ in self.calls if "context" in args)
        self.assertIn("local", context_call)
        self.assertEqual(pin["daemon_id"], self.daemon)

    def test_prepare_publishes_immutable_generation_and_measured_state(self):
        state = self.prepare()
        saved = json.loads((self.runtime.state_dir / "active.json").read_text())
        self.assertEqual(saved, state)
        self.assertEqual(state["image_id"], self.image)
        self.assertGreaterEqual(state["prepare_seconds"], 0)
        self.assertEqual(state["root"], str(self.root))
        generation = self.runtime.state_dir / state["generation"]
        self.assertTrue((generation / "generation.json").is_file())
        self.assertEqual((generation / runtime.BINARIES[0]).stat().st_mode & 0o222, 0)
        self.assertEqual(state["docker"]["daemon_id"], self.daemon)

    def test_prepare_creates_host_owned_cache_for_nonroot_builds(self):
        state = self.prepare()
        cache = self.root / ".cache/mcp/u22"
        self.assertTrue(cache.is_dir())
        self.assertEqual(cache.stat().st_uid, os.getuid())
        _, env = self.runtime.compose_command(state, "mcp-prepare")
        self.assertEqual(env["JOSHUA_MCP_CACHE"], str(cache))

    def test_explicit_image_rebuild_reuses_restricted_build_definition(self):
        with mock.patch.object(runtime, "require_host"), mock.patch.object(
            runtime,
            "source_manifest",
            return_value={"revision": "abc123", "tree_sha256": "abc"},
        ):
            self.runtime.prepare(rebuild_image=True)
        build = next(args for args, _ in self.calls if "build" in args)
        self.assertEqual(build[-2:], ["build", "mcp-build-u22"])
        self.assertEqual(build[:3], ["docker", "--host", "unix:///tmp/docker.sock"])

    def test_failed_reprepare_preserves_the_active_generation(self):
        first = self.prepare()
        self.fail_prepare = True
        with self.assertRaises(runtime.McpError):
            self.prepare()
        self.assertEqual(self.runtime.read_state(), first)
        self.assertEqual(
            len(list((self.runtime.state_dir / "generations").iterdir())), 1
        )

    def test_absent_image_proves_locality_before_sending_project_build_context(self):
        self.project_image_missing = True
        self.bootstrap_image_missing = True
        self.prepare()
        commands = [args for args, _ in self.calls]
        self.assertTrue(any("pull" in args for args in commands))
        pull = next(i for i, args in enumerate(commands) if "pull" in args)
        probe = next(i for i, args in enumerate(commands) if "mcp-probe" in args)
        build = next(i for i, args in enumerate(commands) if "build" in args)
        self.assertLess(pull, probe)
        self.assertLess(probe, build)

    def test_absent_project_image_on_vm_never_uploads_build_context(self):
        self.project_image_missing = True
        self.wrong_probe_boot = True
        with self.assertRaises(runtime.McpError):
            self.prepare()
        self.assertFalse(any("build" in args for args, _ in self.calls))

    def test_same_host_probe_rejects_vm_and_wrong_bind_mount(self):
        self.wrong_probe_boot = True
        with self.assertRaises(runtime.McpError):
            self.prepare()
        self.wrong_probe_boot = False
        self.wrong_probe_nonce = True
        with self.assertRaises(runtime.McpError):
            self.prepare()
        self.assertFalse((self.runtime.state_dir / "active.json").exists())

    def test_each_connection_reprobes_pinned_image_daemon_and_mount(self):
        self.prepare()
        self.assertEqual(sum("mcp-probe" in args for args, _ in self.calls), 1)
        self.runtime.checked_state()
        self.assertEqual(sum("mcp-probe" in args for args, _ in self.calls), 2)

    def test_recorded_preparation_boot_does_not_block_current_host_probe(self):
        state = self.prepare()
        with mock.patch.object(runtime, "host_boot_id", return_value="current-boot"):
            checked = self.runtime.checked_state()
        self.assertEqual(checked["host_boot_id"], state["host_boot_id"])
        self.assertNotEqual(checked["host_boot_id"], "current-boot")

    def test_doctor_uses_prepared_pin_after_current_context_becomes_remote(self):
        state = self.prepare()
        self.runtime.environ["DOCKER_HOST"] = "ssh://different-context"
        self.calls.clear()
        with mock.patch.object(
            runtime,
            "host_status",
            return_value={"status": "supported", "detail": "test"},
        ), mock.patch.object(runtime, "source_manifest", return_value=state["source"]):
            report = self.runtime.doctor()
        self.assertTrue(report["ok"], report)
        self.assertEqual(report["docker"], state["docker"])
        self.assertFalse(any("context" in args for args, _ in self.calls))

    def test_doctor_still_rejects_changed_pinned_daemon_with_remote_current_context(
        self,
    ):
        self.prepare()
        self.runtime.environ["DOCKER_HOST"] = "ssh://different-context"
        self.daemon = "replacement-daemon"
        with mock.patch.object(
            runtime,
            "host_status",
            return_value={"status": "supported", "detail": "test"},
        ):
            report = self.runtime.doctor()
        self.assertFalse(report["ok"])
        self.assertIn("daemon_changed", report["problem_codes"])

    def test_container_operation_restores_existing_signal_handler(self):
        state = self.prepare()

        def handler(_signum, _frame):
            pass

        previous = signal.signal(signal.SIGTERM, handler)
        try:
            self.runtime.run_container(state, "mcp-check")
            self.assertIs(signal.getsignal(signal.SIGTERM), handler)
        finally:
            signal.signal(signal.SIGTERM, previous)

    def test_timed_out_check_cleans_up_its_exact_named_container(self):
        self.prepare()
        self.calls.clear()
        self.fail_check_timeout = True
        with mock.patch.object(runtime, "require_host"):
            with self.assertRaises(runtime.McpError):
                self.runtime.check()
        check = next(args for args, _ in self.calls if "mcp-check" in args)
        name = check[check.index("--name") + 1]
        removed = [args[-1] for args, _ in self.calls if "rm" in args]
        self.assertIn(name, removed)
        self.assertTrue(all(value.startswith("joshua-mcp-") for value in removed))

    def test_removed_config_root_fails_before_starting_any_container(self):
        self.prepare()
        self.runtime.config_root.rmdir()
        self.calls.clear()
        with self.assertRaises(runtime.McpError):
            self.runtime.checked_state()
        self.assertFalse(any("run" in args for args, _ in self.calls))

    def test_protocol_failure_does_not_publish_new_state(self):
        first = self.prepare()
        self.fail_check = True
        with self.assertRaisesRegex(runtime.McpError, "smoke"):
            self.prepare()
        self.assertEqual(self.runtime.read_state(), first)

    def test_check_runs_real_protocol_client_with_optional_candidate(self):
        self.prepare()
        with mock.patch.object(runtime, "require_host"):
            report = self.runtime.check("example/candidate.pbtxt")
        self.assertTrue(report["ok"])
        command = next(args for args, _ in reversed(self.calls) if "mcp-check" in args)
        self.assertEqual(
            command[-3:],
            [
                "/opt/joshua-mcp/tools/mcp/setup_smoke",
                "--config",
                "example/candidate.pbtxt",
            ],
        )

    def test_prepare_refuses_source_change_before_publication(self):
        with mock.patch.object(runtime, "require_host"), mock.patch.object(
            runtime,
            "source_manifest",
            side_effect=[
                {"revision": "a", "tree_sha256": "a"},
                {"revision": "a", "tree_sha256": "b"},
            ],
        ):
            with self.assertRaisesRegex(runtime.McpError, "changed"):
                self.runtime.prepare()
        self.assertFalse((self.runtime.state_dir / "active.json").exists())

    def test_prepare_lock_excludes_another_prepare(self):
        with self.runtime.prepare_lock():
            with self.assertRaisesRegex(runtime.McpError, "prepare.*running"):
                with self.runtime.prepare_lock():
                    self.fail("second prepare acquired the lock")

    def test_runtime_refuses_daemon_switch(self):
        state = self.prepare()
        self.daemon = "different-daemon"
        with self.assertRaisesRegex(runtime.McpError, "daemon.*changed"):
            self.runtime.checked_state()
        self.assertFalse(any("mcp-serve" in args for args, _ in self.calls))
        self.assertEqual(self.runtime.read_state(), state)

    def test_malformed_state_fields_are_reported_as_repairable_state_errors(self):
        state = self.prepare()
        for key, value in (
            ("image_id", None),
            ("docker", {"endpoint": None, "daemon_id": "local"}),
            ("source", None),
        ):
            with self.subTest(key=key):
                bad = dict(state)
                bad[key] = value
                (self.runtime.state_dir / "active.json").write_text(json.dumps(bad))
                with self.assertRaises(runtime.McpError):
                    self.runtime.read_state()

    def test_artifact_change_and_wrong_root_are_rejected(self):
        state = self.prepare()
        artifact = self.runtime.state_dir / state["generation"] / runtime.BINARIES[0]
        artifact.chmod(0o755)
        artifact.write_text("changed")
        with self.assertRaisesRegex(runtime.McpError, "artifact"):
            self.runtime.checked_state()
        state["root"] = "/different/checkout"
        (self.runtime.state_dir / "active.json").write_text(json.dumps(state))
        with self.assertRaisesRegex(runtime.McpError, "root"):
            self.runtime.checked_state()

    def test_serve_command_pins_image_artifacts_endpoint_and_nonroot_user(self):
        state = self.prepare()
        command, env = self.runtime.compose_command(state, "mcp-serve")
        self.assertEqual(command[:3], ["docker", "--host", state["docker"]["endpoint"]])
        self.assertIn("--no-deps", command)
        self.assertNotIn("--build", command)
        self.assertNotIn("--privileged", command)
        self.assertEqual(env["JOSHUA_MCP_IMAGE"], self.image)
        self.assertEqual(env["JOSHUA_MCP_ROOT"], str(self.root))
        self.assertEqual(env["JOSHUA_MCP_USER"], f"{os.getuid()}:{os.getgid()}")
        self.assertEqual(
            env["JOSHUA_MCP_CONFIG_ROOT"], str(self.root / "config/config_preset")
        )

    def test_runtime_uses_prepared_compose_even_if_source_definition_changes(self):
        state = self.prepare()
        command, _ = self.runtime.compose_command(state, "mcp-serve")
        expected = (
            self.runtime.state_dir / state["generation"] / "docker-compose.mcp.yml"
        )
        self.assertEqual(command[command.index("-f") + 1], str(expected))
        (self.root / "docker-compose.mcp.yml").write_text("changed")
        self.assertEqual(expected.read_text(), "services: {}\n")

    def test_orphan_detection_distinguishes_live_owner_from_dead_owner(self):
        self.assertTrue(
            callable(getattr(runtime, "owner_token", None)),
            "Owner identity is not implemented",
        )
        self.prepare()
        with mock.patch.object(self.runtime, "docker") as docker:
            docker.return_value = subprocess.CompletedProcess(
                [],
                0,
                json.dumps(
                    {"ID": "dead", "Labels": "io.joshua.mcp.owner=missing:999999999:0"}
                )
                + "\n"
                + json.dumps(
                    {
                        "ID": "live",
                        "Labels": "io.joshua.mcp.owner="
                        + runtime.owner_token(os.getpid()),
                    }
                )
                + "\n",
                "",
            )
            sessions = self.runtime.containers({"endpoint": "unix:///tmp/docker.sock"})
        self.assertEqual([entry["owner_alive"] for entry in sessions], [False, True])

    def test_doctor_unprepared_is_structured_and_never_builds(self):
        with mock.patch.object(
            runtime,
            "host_status",
            return_value={"status": "supported", "detail": "test"},
        ):
            report = self.runtime.doctor()
        self.assertFalse(report["ok"])
        self.assertEqual(report["status"], "unprepared")
        self.assertEqual(report["orphan_containers"], [])
        self.assertFalse(any("build" in c[0] or "run" in c[0] for c in self.calls))

    def test_explicit_config_root_is_pinned(self):
        state = self.prepare()
        other = self.root / "other"
        other.mkdir()
        item = runtime.Runtime(
            self.root, config_root=other, runner=self.fake_run, environ={}
        )
        with self.assertRaisesRegex(runtime.McpError, "config.root"):
            item.checked_state()
        self.assertEqual(self.runtime.read_state(), state)


class ProtocolInputTest(unittest.TestCase):
    def test_preflight_cannot_consume_an_early_initialize_message(self):
        script = (
            "import json,sys\n"
            f"sys.path.insert(0, {str(HERE)!r})\n"
            "from mcp_runtime import Runtime\n"
            "command=[sys.executable,'-c','import sys; print(sys.stdin.read())']\n"
            "preflight=Runtime('.').run(command)\n"
            "print(json.dumps([preflight.stdout.strip(),sys.stdin.read()]))\n"
        )
        initialize = '{"jsonrpc":"2.0","id":1,"method":"initialize"}\n'
        result = subprocess.run(
            [os.sys.executable, "-c", script],
            input=initialize,
            text=True,
            capture_output=True,
            timeout=10,
            check=True,
        )
        self.assertEqual(json.loads(result.stdout), ["", initialize])


class SignalCleanupProcessTest(unittest.TestCase):
    def test_termination_signals_reap_cli_and_clean_before_releasing_lock(self):
        for signum in (signal.SIGTERM, signal.SIGHUP):
            for service in ("mcp-prepare", "mcp-check", "mcp-probe"):
                with self.subTest(signal=signum, service=service):
                    self.check_signal_cleanup(signum, service)

    def test_image_operations_reap_cli_before_releasing_lock(self):
        for signum in (signal.SIGTERM, signal.SIGHUP):
            for operation in ("image-build", "image-pull"):
                with self.subTest(signal=signum, operation=operation):
                    self.check_signal_cleanup(signum, operation)

    def check_signal_cleanup(self, signum, service):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "config/config_preset").mkdir(parents=True)
            (root / "docker-compose.mcp.yml").write_text("services: {}\n")
            fake_bin = root / "bin"
            fake_bin.mkdir()
            ready = root / "ready.json"
            cleaned = root / "cleaned.json"
            docker = fake_bin / "docker"
            docker.write_text("#!" + os.sys.executable + "\n" + FAKE_BLOCKING_DOCKER)
            docker.chmod(0o755)
            script = root / "wrapper.py"
            state = {
                "docker": {
                    "endpoint": "unix:///fake-local.sock",
                    "daemon_id": "test-daemon",
                },
                "image_id": "sha256:test",
                "generation": "unused",
            }
            script.write_text(
                "import os,sys\nfrom pathlib import Path\n"
                + f"sys.path.insert(0, {str(HERE)!r})\n"
                + "from mcp_runtime import Runtime\n"
                + "runtime = Runtime(Path(os.environ['MCP_TEST_ROOT']))\n"
                + f"state = {state!r}\n"
                + "with runtime.prepare_lock():\n"
                + f"    runtime.run_container(state, {service!r}, timeout=60)\n"
            )
            if service.startswith("image-"):
                script.write_text(
                    "import os,sys\nfrom pathlib import Path\n"
                    + f"sys.path.insert(0, {str(HERE)!r})\n"
                    + IMAGE_PREPARE_WRAPPER
                )
            env = dict(os.environ)
            env.update(
                {
                    "PATH": str(fake_bin) + os.pathsep + env["PATH"],
                    "MCP_TEST_ROOT": str(root),
                    "MCP_TEST_READY": str(ready),
                    "MCP_TEST_CLEANED": str(cleaned),
                    "MCP_TEST_OPERATION": service,
                }
            )
            process = subprocess.Popen(
                [os.sys.executable, str(script)],
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            child = None
            try:
                deadline = time.monotonic() + 5
                while (
                    not ready.exists()
                    and process.poll() is None
                    and time.monotonic() < deadline
                ):
                    time.sleep(0.01)
                self.assertTrue(ready.exists(), "Fake Docker did not start")
                child = json.loads(ready.read_text())
                process.send_signal(signum)
                try:
                    stdout, stderr = process.communicate(timeout=8)
                except subprocess.TimeoutExpired:
                    os.kill(child["pid"], signal.SIGKILL)
                    process.communicate(timeout=3)
                    self.fail("Docker CLI survived wrapper termination")
                self.assertTrue(
                    cleaned.exists(),
                    f"Scoped cleanup missing after {signum}: {stdout} {stderr}",
                )
                cleanup = json.loads(cleaned.read_text())
                self.assertEqual(cleanup["name"], child["name"])
                self.assertTrue(
                    cleanup["lock_held"], "Preparation lock released before cleanup"
                )
                self.assertFalse(
                    cleanup["child_alive"], "Docker CLI was not reaped before cleanup"
                )
                self.assertEqual(process.returncode, 128 + signum)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=3)
                if child is not None:
                    try:
                        os.kill(child["pid"], signal.SIGKILL)
                    except ProcessLookupError:
                        pass


FAKE_BLOCKING_DOCKER = r"""
import fcntl
import json
import os
import signal
import shutil
import sys
from pathlib import Path

args = sys.argv[1:]
if "context" in args:
    print(json.dumps([{"Endpoints": {"docker": {"Host": "unix:///fake-local.sock"}}}]))
elif "image" in args and "inspect" in args:
    if os.environ["MCP_TEST_OPERATION"] == "image-pull":
        raise SystemExit(1)
    print(json.dumps([{"Id": "sha256:test", "Os": "linux", "Architecture": "amd64"}]))
elif "info" in args:
    info = {"ID": "test-daemon", "OSType": "linux", "Architecture": "x86_64"}
    print(json.dumps(info))
elif "run" in args or "build" in args or "pull" in args:
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    signal.signal(signal.SIGHUP, signal.SIG_IGN)
    ready = Path(os.environ["MCP_TEST_READY"])
    temporary = ready.with_suffix(".tmp")
    name = (args[args.index("--name") + 1] if "--name" in args
            else os.environ["MCP_TEST_OPERATION"])
    temporary.write_text(json.dumps({"pid": os.getpid(), "name": name}))
    temporary.replace(ready)
    while True:
        signal.pause()
elif "rm" in args:
    ready = json.loads(Path(os.environ["MCP_TEST_READY"]).read_text())
    lock = Path(os.environ["MCP_TEST_ROOT"]) / "dist/mcp/u22/prepare.lock"
    with lock.open("a") as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            held = False
        except BlockingIOError:
            held = True
    result = {
        "name": args[-1], "lock_held": held,
        "child_alive": Path(f"/proc/{ready['pid']}").exists(),
    }
    Path(os.environ["MCP_TEST_CLEANED"]).write_text(json.dumps(result))
"""


IMAGE_PREPARE_WRAPPER = r"""
import contextlib
import json
import mcp_runtime

mcp_runtime.require_host = lambda: None
mcp_runtime.source_manifest = lambda root: {"revision": "test", "tree_sha256": "test"}
runtime = mcp_runtime.Runtime(Path(os.environ["MCP_TEST_ROOT"]))
runtime.probe_docker = lambda *args, **kwargs: None
original_lock = runtime.prepare_lock

@contextlib.contextmanager
def observed_lock():
    with original_lock():
        try:
            yield
        finally:
            ready = json.loads(Path(os.environ["MCP_TEST_READY"]).read_text())
            result = {
                "name": ready["name"], "lock_held": True,
                "child_alive": Path(f"/proc/{ready['pid']}").exists(),
            }
            Path(os.environ["MCP_TEST_CLEANED"]).write_text(json.dumps(result))

runtime.prepare_lock = observed_lock
runtime.prepare(rebuild_image=os.environ["MCP_TEST_OPERATION"] == "image-build")
"""


class ComposeContractTest(unittest.TestCase):
    def test_effective_prepare_service_is_nonroot_with_host_cache(self):
        if not shutil.which("docker"):
            self.skipTest("Docker Compose CLI is not installed")
        version = subprocess.run(
            ["docker", "compose", "version"], capture_output=True, check=False
        )
        if version.returncode:
            self.skipTest("Docker Compose CLI is not installed")
        item = runtime.Runtime(HERE.parent)
        state = {"image_id": "sha256:test", "generation": "unused"}
        model = json.loads(
            subprocess.check_output(
                [
                    "docker",
                    "compose",
                    "--env-file",
                    "/dev/null",
                    "-f",
                    str(HERE.parent / "docker-compose.mcp.yml"),
                    "config",
                    "--format",
                    "json",
                ],
                env=item.compose_env(state),
                text=True,
            )
        )
        prepare = model["services"]["mcp-prepare"]
        self.assertEqual(prepare["user"], f"{os.getuid()}:{os.getgid()}")
        self.assertEqual(prepare["cap_drop"], ["ALL"])
        self.assertFalse(prepare.get("cap_add"))
        self.assertFalse(prepare.get("privileged"))
        self.assertEqual(prepare["environment"]["HOME"], "/mcp-cache")
        # Humble has no passwd entry for an arbitrary host UID. Bazel needs
        # USER even though kernel permissions continue to use the numeric UID.
        self.assertTrue(prepare["environment"].get("USER"))
        cache = next(
            mount for mount in prepare["volumes"] if mount["target"] == "/mcp-cache"
        )
        self.assertEqual(cache["type"], "bind")
        self.assertEqual(cache["source"], str(item.cache_dir))
        # Compose v2 omits false booleans from its normalized JSON model.
        self.assertFalse(cache.get("bind", {}).get("create_host_path", False))


@unittest.skipUnless(
    os.environ.get("JOSHUA_MCP_DOCKER_TESTS") == "1",
    "Opt-in real Docker ownership regression",
)
class DockerOwnershipTest(unittest.TestCase):
    def test_interrupted_copy_is_host_owned_and_removed(self):
        item = runtime.Runtime(
            HERE.parent, stack=os.environ.get("JOSHUA_MCP_TEST_STACK", "u22")
        )
        item.state_dir.mkdir(parents=True, exist_ok=True)
        item.cache_dir.mkdir(parents=True, exist_ok=True)
        pin = item.resolve_docker()
        image = item.image_info(pin, runtime.STACKS[item.stack][0])
        stage = Path(tempfile.mkdtemp(prefix=".ownership-test-", dir=item.state_dir))
        state = {"docker": pin, "image_id": image, "generation": "unused"}
        code = """
import sys
import time
from pathlib import Path
sys.path.insert(0, '/workspace/scripts')
import mcp_container_prepare as pack
src = Path('/tmp/ownership-source')
src.mkdir()
(src / 'file').write_text('source')
def interrupted_copy(source, destination):
    destination.write_text('partial')
    Path('/artifacts/ready').write_text('ready')
    time.sleep(60)
pack.shutil.copy2 = interrupted_copy
pack.copy_tree(src, Path('/artifacts/copied/nested'), [src])
"""
        script = (
            "import sys\nfrom pathlib import Path\n"
            + f"sys.path.insert(0,{str(HERE)!r})\n"
            + "from mcp_runtime import Runtime,remove_staging\n"
            + f"item=Runtime({str(item.root)!r},stack={item.stack!r})\n"
            + f"state={state!r}\n"
            + f"stage=Path({str(stage)!r})\n"
            + f"command=['python3','-c',{code!r}]\n"
            + "try:\n"
            + "    item.run_container(\n"
            + "        state,'mcp-prepare',command,artifact_dir=stage,timeout=60)\n"
            + "finally:\n    remove_staging(stage)\n"
        )
        process = subprocess.Popen(
            [os.sys.executable, "-c", script],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            deadline = time.monotonic() + 15
            while (
                not (stage / "ready").exists()
                and process.poll() is None
                and time.monotonic() < deadline
            ):
                time.sleep(0.02)
            self.assertTrue(
                (stage / "ready").exists(), "Docker copy fixture did not start"
            )
            self.assertTrue(
                all(path.stat().st_uid == os.getuid() for path in stage.rglob("*"))
            )
            process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=15)
            self.assertEqual(process.returncode, 128 + signal.SIGTERM, stdout + stderr)
            self.assertFalse(
                stage.exists(), "Interrupted staging directory was not removed"
            )
        finally:
            if process.poll() is None:
                process.terminate()
                process.communicate(timeout=15)
            runtime.remove_staging(stage)


class PackagingTest(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(packager, "The artifact packager has not been implemented")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_runfiles_are_copied_with_external_symlinks_dereferenced(self):
        allowed = self.root / "bazel"
        allowed.mkdir()
        external = allowed / "external"
        external.mkdir()
        (external / "dependency.py").write_text("VALUE = 1\n")
        runfiles = allowed / "app.runfiles"
        runfiles.mkdir()
        (runfiles / "dep").symlink_to(external, target_is_directory=True)
        out = self.root / "prepared"
        packager.copy_tree(runfiles, out, [allowed])
        self.assertEqual((out / "dep/dependency.py").read_text(), "VALUE = 1\n")
        self.assertFalse(any(p.is_symlink() for p in out.rglob("*")))

    def test_runfiles_reject_special_files_symlink_cycles_and_outside_roots(self):
        allowed = self.root / "allowed"
        allowed.mkdir()
        outside = self.root / "secret"
        outside.write_text("private")
        (allowed / "escape").symlink_to(outside)
        with self.assertRaisesRegex(RuntimeError, "outside"):
            packager.copy_tree(allowed, self.root / "out", [allowed])
        (allowed / "escape").unlink()
        (allowed / "loop").symlink_to(allowed, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, "cycle"):
            packager.copy_tree(allowed, self.root / "out2", [allowed])
        (allowed / "loop").unlink()
        os.mkfifo(allowed / "pipe")
        with self.assertRaisesRegex(RuntimeError, "regular"):
            packager.copy_tree(allowed, self.root / "out3", [allowed])


if __name__ == "__main__":
    unittest.main()
