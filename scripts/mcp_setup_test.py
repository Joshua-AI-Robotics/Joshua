"""Registration regression tests; real client checks use temporary profiles."""

import io
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import mcp_setup as setup


class RegistrationTests(unittest.TestCase):
    def test_doctor_reports_registration_attention_without_repair(self):
        import mcp_runtime

        with patch.object(mcp_runtime, "Runtime") as runtime:
            runtime.return_value.root = Path("/repo")
            runtime.return_value.stack = "u22"
            runtime.return_value.doctor.return_value = {
                "ok": True,
                "status": "ready",
                "problems": [],
            }
            status = {"client": {"status": "attention", "detail": "disabled"}}
            with patch.object(
                setup, "registration_status", return_value=status
            ), patch.object(setup.sys, "stdout", io.StringIO()) as output:
                self.assertEqual(mcp_runtime.main(["doctor"]), 1)
            report = json.loads(output.getvalue())
            self.assertEqual(report["registrations"], status)
            runtime.return_value.prepare.assert_not_called()

    def test_setup_reuses_ready_generation(self):
        import mcp_runtime

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = {
                "prepare_seconds": 1.0,
                "root": str(root),
                "stack": "u22",
                "config_root": str(root / "config/config_preset"),
            }
            active = root / "dist/mcp/u22/active.json"
            active.parent.mkdir(parents=True)
            active.write_text(json.dumps(state))
            with patch.object(mcp_runtime, "Runtime") as runtime:
                runtime.return_value.doctor.return_value = {"ok": True, "state": state}
                with patch.object(setup, "choose_client", return_value="manual"), patch(
                    "builtins.print"
                ):
                    self.assertEqual(setup.main(["--root", str(root)]), 2)
                runtime.return_value.prepare.assert_not_called()
                runtime.return_value.check.assert_called_once_with()

    def test_setup_conflict_preserves_active_state_before_preparation(self):
        import mcp_runtime

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            active = root / "dist/mcp/u22/active.json"
            active.parent.mkdir(parents=True)
            original = json.dumps({"config_root": str(root / "original")})
            active.write_text(original)
            existing = setup.server_entry(root, "u22", root / "original")

            def prepare_new_state():
                active.write_text(json.dumps({"config_root": str(root / "other")}))
                return {"prepare_seconds": 1.0}

            with patch.object(mcp_runtime, "Runtime") as runtime, patch.object(
                setup, "choose_client", return_value="codex"
            ), patch.object(setup, "client_entry", return_value=existing), patch.object(
                setup, "offer_bootstrap"
            ) as bootstrap, patch.object(
                setup.sys, "stderr", io.StringIO()
            ):
                runtime.return_value.doctor.return_value = {"ok": False}
                runtime.return_value.prepare.side_effect = prepare_new_state
                self.assertEqual(
                    setup.main(
                        ["--root", str(root), "--config-root", str(root / "other")]
                    ),
                    1,
                )
                self.assertEqual(active.read_text(), original)
                runtime.return_value.prepare.assert_not_called()
                bootstrap.assert_not_called()

    def test_other_clients_and_manual_setup_preserve_shared_configuration(self):
        import mcp_runtime

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            active = root / "dist/mcp/u22/active.json"
            active.parent.mkdir(parents=True)
            original = json.dumps(
                {
                    "root": str(root),
                    "stack": "u22",
                    "config_root": str(root / "original"),
                }
            )

            def prepare_new_state():
                active.write_text(json.dumps({"config_root": str(root / "other")}))
                return {"prepare_seconds": 1.0}

            for client in ("claude", "manual"):
                active.write_text(original)
                with self.subTest(client=client), patch.object(
                    mcp_runtime, "Runtime"
                ) as runtime, patch.object(
                    setup, "choose_client", return_value=client
                ), patch.object(
                    setup, "check_registration", return_value=False
                ), patch.object(
                    setup, "register", return_value="added"
                ), patch.object(
                    setup, "record_registration"
                ), patch.object(
                    setup.subprocess,
                    "run",
                    return_value=subprocess.CompletedProcess([], 0),
                ), patch.object(
                    setup.sys, "stdout", io.StringIO()
                ), patch.object(
                    setup.sys, "stderr", io.StringIO()
                ):
                    runtime.return_value.doctor.return_value = {"ok": False}
                    runtime.return_value.prepare.side_effect = prepare_new_state
                    code = setup.main(
                        ["--root", str(root), "--config-root", str(root / "other")]
                    )
                    self.assertEqual(active.read_text(), original)
                    self.assertEqual(code, 1)
                    runtime.return_value.prepare.assert_not_called()

    def test_doctor_compares_registration_with_prepared_configuration(self):
        import mcp_runtime

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entry = setup.server_entry(root, "u22", root / "original")
            state_dir = root / "dist/mcp/u22"
            state_dir.mkdir(parents=True)
            (state_dir / "registration.json").write_text(
                json.dumps({"clients": {"codex": entry}})
            )
            for config, expected in (
                ("original", "saved"),
                ("other", "attention"),
                (None, "attention"),
            ):
                with self.subTest(config=config), patch.object(
                    mcp_runtime, "Runtime"
                ) as runtime, patch.object(setup, "check_project_shadow"), patch.object(
                    setup, "client_entry", return_value=entry
                ), patch.object(
                    mcp_runtime.sys, "stdout", io.StringIO()
                ) as output:
                    runtime.return_value.root = root
                    runtime.return_value.stack = "u22"
                    state = (
                        None
                        if config is None
                        else {
                            "root": str(root),
                            "stack": "u22",
                            "config_root": str(root / config),
                        }
                    )
                    runtime.return_value.doctor.return_value = {
                        "ok": True,
                        "status": "ready",
                        "problems": [],
                        "state": state,
                    }
                    code = mcp_runtime.main(["doctor"])
                    result = json.loads(output.getvalue())
                    self.assertEqual(
                        result["registrations"]["codex"]["status"], expected
                    )
                    self.assertEqual(code, 0 if expected == "saved" else 1)

    def test_metadata_retains_multiple_clients(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = root / "dist/mcp/u22"
            state.mkdir(parents=True)
            entry = {"command": "/server", "args": ["serve"]}
            setup.record_registration(root, "u22", "codex", entry)
            setup.record_registration(root, "u22", "claude", entry)
            saved = json.loads((state / "registration.json").read_text())
            self.assertEqual(set(saved["clients"]), {"codex", "claude"})

    def test_generic_fragment_uses_absolute_launcher_and_workspace(self):
        root = Path("/tmp/work tree")
        entry = setup.server_entry(root, "u24", root / "config")
        self.assertEqual(entry["command"], str(root / "scripts/joshua_mcp.sh"))
        self.assertEqual(
            entry["args"],
            [
                "serve",
                "--stack",
                "u24",
                "--root",
                str(root),
                "--config-root",
                str(root / "config"),
            ],
        )

    def test_matching_entry_keeps_additional_policy(self):
        entry = {"command": "/x", "args": ["serve"]}
        self.assertTrue(setup.same_server({**entry, "disabledTools": ["a"]}, entry))
        self.assertFalse(setup.same_server({**entry, "args": ["other"]}, entry))
        self.assertFalse(setup.same_server({**entry, "enabled": False}, entry))
        self.assertFalse(setup.same_server({**entry, "env": {"X": "Y"}}, entry))

    def test_json_adapter_preserves_conflicting_registration(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)
            config = profile / ".claude.json"
            original = json.dumps(
                {
                    "mcpServers": {"joshua": {"command": "other", "args": []}},
                    "other": 42,
                }
            )
            config.write_text(original)
            with patch.dict(os.environ, {"CLAUDE_CONFIG_DIR": directory}):
                with self.assertRaisesRegex(setup.SetupError, "conflict"):
                    setup.register(
                        "claude", {"command": "/new", "args": []}, Path(directory)
                    )
            self.assertEqual(config.read_text(), original)

    def test_json_adapter_idempotent_without_calling_cli(self):
        with tempfile.TemporaryDirectory() as directory:
            entry = {"command": "/new", "args": ["serve"]}
            (Path(directory) / ".claude.json").write_text(
                json.dumps({"mcpServers": {"joshua": entry}})
            )
            with patch.dict(os.environ, {"CLAUDE_CONFIG_DIR": directory}), patch.object(
                setup, "run_client"
            ) as run:
                setup.register("claude", entry, Path(directory))
            run.assert_not_called()

    def test_json_adapter_rejects_project_shadow(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / ".mcp.json").write_text(
                json.dumps({"mcpServers": {"joshua": {"command": "other"}}})
            )
            with self.assertRaisesRegex(setup.SetupError, "shadow"):
                setup.check_project_shadow(
                    "claude", root, {"command": "/new", "args": []}
                )

    def test_selection_is_explicit_when_multiple_clients_noninteractive(self):
        with patch.object(setup.shutil, "which", return_value="/cli"), patch.object(
            setup.sys.stdin, "isatty", return_value=False
        ):
            with self.assertRaisesRegex(setup.SetupError, "--client"):
                setup.choose_client("auto")

    def test_missing_clients_produce_manual_fallback(self):
        with patch.object(setup.shutil, "which", return_value=None):
            self.assertEqual(setup.choose_client("auto"), "manual")

    def test_client_failure_does_not_print_unrelated_secret(self):
        from subprocess import CompletedProcess

        with patch.object(
            setup.subprocess,
            "run",
            return_value=CompletedProcess([], 1, "secret", "secret"),
        ):
            with self.assertRaises(setup.SetupError) as failure:
                setup.run_client(["client"], Path("/tmp"))
        self.assertNotIn("secret", str(failure.exception))

    def test_docker_access_failure_never_offers_installation(self):
        with patch.object(setup.subprocess, "run") as run, patch(
            "builtins.input"
        ) as ask:
            setup.offer_bootstrap(
                Path("/repo"), {"problem_codes": ["docker_unavailable"]}
            )
        run.assert_not_called()
        ask.assert_not_called()


@unittest.skipUnless(
    os.environ.get("JOSHUA_TEST_CLIENTS") == "1",
    "opt-in real CLI tests use temporary profiles",
)
class NativeClientTests(unittest.TestCase):
    def test_home_project_entry_is_not_mistaken_for_user_registration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            home = root / "home"
            workspace = root / "workspace"
            other = root / "other-project"
            profile = root / "alternate-profile"
            profile.mkdir()
            for project in (home, workspace, other):
                project.mkdir()
                subprocess.run(["git", "init", "-q", str(project)], check=True)
            entry = setup.server_entry(workspace, "u22", workspace / "config")
            (home / ".codex").mkdir()
            (home / ".codex/config.toml").write_text(
                "[mcp_servers.joshua]\ncommand = "
                + json.dumps(entry["command"])
                + "\nargs = "
                + json.dumps(entry["args"])
                + "\n"
            )
            (profile / "config.toml").write_text(
                f'[projects.{json.dumps(str(home))}]\ntrust_level = "trusted"\n'
            )
            with patch.dict(
                os.environ, {"HOME": str(home), "CODEX_HOME": str(profile)}
            ):
                self.assertTrue(
                    setup.same_server(setup.client_entry("codex", home), entry)
                )
                self.assertEqual(setup.register("codex", entry, workspace), "added")
                self.assertTrue(
                    setup.same_server(setup.client_entry("codex", other), entry)
                )

    def test_native_registration_and_second_project_readback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = root / "workspace"
            workspace.mkdir()
            subprocess.run(["git", "init", "-q", str(workspace)], check=True)
            other = root / "other-project"
            other.mkdir()
            subprocess.run(["git", "init", "-q", str(other)], check=True)
            (root / "toml-profile").mkdir()
            entry = setup.server_entry(workspace, "u22", workspace / "config")
            with patch.dict(
                os.environ,
                {
                    "CODEX_HOME": str(root / "toml-profile"),
                    "CLAUDE_CONFIG_DIR": str(root / "json-profile"),
                    "DISABLE_TELEMETRY": "1",
                },
            ):
                for client in ("codex", "claude"):
                    with self.subTest(client=client):
                        self.assertEqual(
                            setup.register(client, entry, workspace), "added"
                        )
                        self.assertEqual(
                            setup.register(client, entry, workspace), "existing"
                        )
                        self.assertTrue(
                            setup.same_server(setup.client_entry(client, other), entry)
                        )
                        setup.check_project_shadow(client, other, entry)
                        if client == "claude":
                            effective = setup.run_client(
                                ["claude", "mcp", "get", "joshua"], other
                            )
                            self.assertIn("Command: " + entry["command"], effective)
                            local = {
                                "type": "stdio",
                                "command": "/bin/echo",
                                "args": ["shadow-server"],
                            }
                            setup.run_client(
                                [
                                    "claude",
                                    "mcp",
                                    "add-json",
                                    "--scope",
                                    "local",
                                    "joshua",
                                    json.dumps(local),
                                ],
                                other,
                            )
                            effective = setup.run_client(
                                ["claude", "mcp", "get", "joshua"], other
                            )
                            self.assertIn("Args: shadow-server", effective)
                            with self.assertRaisesRegex(setup.SetupError, "shadow"):
                                setup.check_project_shadow(client, other, entry)
                        conflict = {**entry, "command": "/other-checkout"}
                        with self.assertRaises(setup.SetupError):
                            setup.register(client, conflict, workspace)
                        self.assertTrue(
                            setup.same_server(setup.client_entry(client, other), entry)
                        )


if __name__ == "__main__":
    unittest.main()
