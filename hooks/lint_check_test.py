"""Exercise lint enforcement with real tools in the Docker lint service."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECK = ROOT / "hooks/lint_check.sh"


class LintCheckTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.git("init", "-q", "-b", "develop")
        self.git("config", "user.email", "lint-test@example.invalid")
        self.git("config", "user.name", "Lint test")
        self.write("old.py", "x= 1\n")
        shutil.copy(ROOT / ".clang-format", self.repo)
        self.git("add", ".")
        self.git("commit", "-qm", "baseline")
        self.git("switch", "-qc", "feature")

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.repo)

    def write(self, name, text):
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def check(self, *args, env=None, cwd=None, direct=False):
        command = (
            [sys.executable, str(ROOT / "hooks/lint_check.py")]
            if direct
            else ["/bin/bash", str(CHECK)]
        )
        return subprocess.run(
            [*command, *args],
            cwd=cwd or self.repo,
            env=env,
            capture_output=True,
            text=True,
        )

    def assert_passes(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def restricted_environment(self, *tools):
        directory = self.repo / "bin"
        directory.mkdir()
        for name in ("python3", "git", "dirname", *tools):
            (directory / name).symlink_to(shutil.which(name))
        return dict(os.environ, PATH=str(directory))

    def test_unchanged_files_and_ui_are_excluded(self):
        self.write("ui/bad.py", "x= 1\n")
        self.write("ui/BUILD", 'filegroup(name="x")\n')
        self.assert_passes(self.check())
        self.assert_passes(self.check("ui/bad.py", "ui/BUILD"))

    def test_yaml_consumes_all_documents_and_handles_quoted_filename(self):
        name = "config's settings.yaml"
        self.write(name, "first: valid\n---\nbroken: [\n")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("YAML syntax error", result.stderr)
        self.assertIn(name, result.stderr)

    def test_compose_tags_and_multiple_yaml_documents(self):
        self.write(
            "compose.yml",
            "services:\n  app:\n    devices: !reset []\n"
            "    ports: !override [1234]\n---\nsecond: valid\n",
        )
        self.assert_passes(self.check())

    def test_unsorted_imports_fail_then_fix(self):
        self.write("imports.py", "import sys\nimport os\n\nprint(sys.path, os.name)\n")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("isort", result.stderr)
        self.assert_passes(self.check("--fix"))
        self.assert_passes(self.check())

    def test_black_and_flake8_are_enforced(self):
        self.write("format.py", "x= 1\n")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("black", result.stderr)
        self.assert_passes(self.check("--fix"))
        self.write("undefined.py", "print(missing_name)\n")
        result = self.check("--fix")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("F821", result.stderr)

    def test_build_files_are_discovered_and_fixed(self):
        for name in ("BUILD", "nested/BUILD.bazel"):
            self.write(name, 'filegroup(name="sample",srcs=[])\n')
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("buildifier", result.stderr)
        self.assert_passes(self.check("--fix"))
        self.assert_passes(self.check())

    def test_cpp_is_checked_and_fixed(self):
        self.write("sample.cc", "int main(){return 0;}\n")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("clang-format", result.stderr)
        self.assert_passes(self.check("--fix"))
        self.assert_passes(self.check())

    def test_missing_tools_fail_in_check_and_fix_modes(self):
        self.write("sample.py", "x = 1\n")
        env = self.restricted_environment()
        for args in ((), ("--fix",)):
            with self.subTest(args=args):
                result = self.check(*args, env=env, direct=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Missing lint tools: black, flake8, isort", result.stderr)

    def test_formatter_failure_is_not_swallowed_in_fix_mode(self):
        self.write("sample.py", "x = 1\n")
        env = self.restricted_environment("isort", "flake8")
        failing = self.write("bin/black", "#!/bin/sh\nexit 42\n")
        failing.chmod(0o755)
        result = self.check("--fix", env=env, direct=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Failed: black", result.stderr)

    def test_committed_staged_unstaged_untracked_and_renamed_files(self):
        self.write("committed.yml", "bad: [\n")
        self.git("add", "committed.yml")
        self.git("commit", "-qm", "change")
        self.write("staged.yml", "bad: [\n")
        self.git("add", "staged.yml")
        self.write("old.py", "x= 2\n")
        self.write("untracked.yml", "bad: [\n")
        self.git("mv", "old.py", "renamed.py")
        result = self.check("--base", "develop")
        self.assertNotEqual(result.returncode, 0)
        for name in ("committed.yml", "staged.yml", "untracked.yml", "renamed.py"):
            self.assertIn(name, result.stderr)

    def test_deleted_files_and_diverged_base(self):
        self.git("switch", "-q", "develop")
        self.write("base_only.py", "x= 1\n")
        self.git("add", ".")
        self.git("commit", "-qm", "base advanced")
        self.git("switch", "-q", "feature")
        self.git("rm", "-q", "old.py")
        self.assert_passes(self.check("--base", "develop"))
        self.assertNotEqual(self.check("--base", "missing-ref").returncode, 0)

    def test_explicit_files_from_subdirectory(self):
        path = self.write("nested/a.py", "x= 1\n")
        self.assert_passes(self.check("--fix", "a.py", cwd=path.parent))
        self.assert_passes(self.check("a.py", cwd=path.parent))

    def test_whitespace_and_missing_newline(self):
        self.write("spaces.yml", "key: value  ")
        self.assertNotEqual(self.check().returncode, 0)
        self.assert_passes(self.check("--fix"))
        self.assertEqual((self.repo / "spaces.yml").read_text(), "key: value\n")
        self.assert_passes(self.check())

    def fake_docker(self, exit_code=0):
        directory = self.repo / "docker-bin"
        directory.mkdir()
        executable = directory / "docker"
        executable.write_text(
            f"#!{sys.executable}\n"
            "import pathlib, sys\n"
            "if sys.argv[1:3] == ['compose', 'build']:\n"
            "    sys.exit(0)\n"
            f"pathlib.Path({str(self.repo / 'docker-input')!r}).write_bytes("
            "sys.stdin.buffer.read())\n"
            f"pathlib.Path({str(self.repo / 'docker-args')!r}).write_text("
            "repr(sys.argv))\n"
            f"sys.exit({exit_code})\n"
        )
        executable.chmod(0o755)
        return dict(os.environ, PATH=f"{directory}:{os.environ['PATH']}")

    def test_worktree_discovery_precedes_docker_and_preserves_filenames(self):
        worktree = self.repo / "linked"
        self.git("worktree", "add", "-qb", "linked", str(worktree))
        name = "quoted' and spaced\nfile.yaml"
        (worktree / name).write_text("key: value\n")
        result = self.check(
            "--docker", "--verbose", env=self.fake_docker(), cwd=worktree
        )
        self.assert_passes(result)
        self.assertEqual(
            (self.repo / "docker-input").read_bytes(), os.fsencode(name) + b"\0"
        )
        command = (self.repo / "docker-args").read_text()
        self.assertIn("--files-from-stdin0", command)
        self.assertIn(f"{os.getuid()}:{os.getgid()}", command)

    def test_docker_failure_is_propagated(self):
        self.write("sample.yaml", "key: value\n")
        result = self.check("--docker", env=self.fake_docker(exit_code=19))
        self.assertEqual(result.returncode, 19)

    def test_stdin_file_list_needs_no_git_metadata(self):
        directory = self.repo / "container-checkout"
        directory.mkdir()
        (directory / ".git").write_text("gitdir: /unmounted-host/worktrees/test\n")
        (directory / "bad.yaml").write_text("broken: [\n")
        command = [
            sys.executable,
            str(ROOT / "hooks/lint_check.py"),
            "--files-from-stdin0",
        ]
        empty = subprocess.run(command, input=b"", cwd=directory, capture_output=True)
        self.assertEqual(empty.returncode, 0, empty.stderr)
        invalid = subprocess.run(
            command, input=b"bad.yaml\0", cwd=directory, capture_output=True
        )
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn(b"YAML syntax error", invalid.stderr)
        (directory / "bad.yaml").write_text("valid: value\n")
        valid = subprocess.run(
            command, input=b"bad.yaml\0", cwd=directory, capture_output=True
        )
        self.assertEqual(valid.returncode, 0, valid.stderr)

    def test_container_hook_does_not_invoke_docker(self):
        self.write("sample.yaml", "key: value\n")
        self.assert_passes(self.check(env=self.fake_docker(exit_code=19)))
        self.assertFalse((self.repo / "docker-input").exists())


if __name__ == "__main__":
    unittest.main()
