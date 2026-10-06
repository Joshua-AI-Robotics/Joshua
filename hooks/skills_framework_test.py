"""Exercise the skill guard and installer against an isolated library."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class SkillsFrameworkTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        for directory in ("hooks", "scripts", "docs/skills"):
            (self.repo / directory).mkdir(parents=True, exist_ok=True)
        for name in ("skills_doc_check.sh", "skill_metadata_check.py"):
            source = ROOT / "hooks" / name
            if source.exists():
                shutil.copy2(source, self.repo / "hooks" / name)
        shutil.copy2(
            ROOT / "scripts/install-skills.sh", self.repo / "scripts/install-skills.sh"
        )
        self.library = self.repo / "docs/skills"
        self.skill = self.library / "joshua-test"
        self.skill.mkdir()
        self.metadata("name: joshua-test\ndescription: Use when testing")
        (self.library / "README.md").write_text("[Test](joshua-test/SKILL.md)\n")
        self.home = self.root / "home"
        self.home.mkdir()
        self.env = dict(
            os.environ, HOME=str(self.home), CODEX_HOME=str(self.home / ".codex")
        )

    def metadata(self, fields):
        (self.skill / "SKILL.md").write_text(f"---\n{fields}\n---\n# Test\n")

    def run_script(self, relative, *args):
        return subprocess.run(
            ["bash", str(self.repo / relative), *args],
            env=self.env,
            capture_output=True,
            text=True,
        )

    def guard(self):
        return self.run_script("hooks/skills_doc_check.sh")

    def install(self, *args):
        result = self.run_script("scripts/install-skills.sh", *args)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_valid_metadata(self):
        self.assertEqual(self.guard().returncode, 0)

    def test_invalid_yaml_is_rejected(self):
        self.metadata("name: joshua-test\ndescription: Use when testing: a component")
        self.assertNotEqual(self.guard().returncode, 0)

    def test_required_fields_are_nonempty_strings(self):
        for value in ("null", "true", "123", "[]", "{}", "''", "'   '"):
            with self.subTest(value=value):
                self.metadata(f"name: joshua-test\ndescription: {value}")
                self.assertNotEqual(self.guard().returncode, 0)

    def test_missing_fields_mismatched_names_and_nonmapping_headers(self):
        for fields in (
            "name: joshua-test",
            "description: Use when testing",
            "name: joshua-other\ndescription: Use when testing",
            "[name, description]",
        ):
            with self.subTest(fields=fields):
                self.metadata(fields)
                self.assertNotEqual(self.guard().returncode, 0)

    def test_crlf_header(self):
        path = self.skill / "SKILL.md"
        path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.assertEqual(self.guard().returncode, 0)

    def test_quoted_and_multiline_yaml(self):
        for description in (
            '"Use when testing: a component"',
            ">-\n  Use when testing",
        ):
            with self.subTest(description=description):
                self.metadata(f"name: 'joshua-test'\ndescription: {description}")
                self.assertEqual(self.guard().returncode, 0)

    def test_absent_agents_and_dry_run_do_not_write(self):
        self.install()
        self.assertEqual(list(self.home.iterdir()), [])
        (self.home / ".codex").mkdir()
        self.install("--dry-run")
        self.assertFalse((self.home / ".codex/skills").exists())

    def test_both_agents_repeat_install_and_last_skill_removal(self):
        for agent in (".codex", ".claude"):
            (self.home / agent).mkdir()
        self.install()
        self.install()
        links = [self.home / a / "skills/joshua-test" for a in (".codex", ".claude")]
        for link in links:
            self.assertTrue(link.is_symlink())
            self.assertEqual(link.resolve(), self.skill)
        shutil.rmtree(self.skill)
        self.assertNotEqual(self.guard().returncode, 0)
        self.install("--dry-run")
        self.assertTrue(all(link.is_symlink() for link in links))
        self.install()
        self.assertTrue(all(not link.is_symlink() for link in links))
        (self.library / "README.md").write_text("# Empty library\n")
        self.assertEqual(self.guard().returncode, 0)

    def test_preserve_local_data_and_foreign_links_unless_forced(self):
        target = self.home / ".codex/skills/joshua-test"
        target.mkdir(parents=True)
        (target / "local.txt").write_text("keep")
        self.install()
        self.assertEqual((target / "local.txt").read_text(), "keep")
        self.install("--force", "--dry-run")
        self.assertTrue((target / "local.txt").exists())
        self.install("--force")
        self.assertEqual(target.resolve(), self.skill)
        target.unlink()
        foreign = self.root / "foreign"
        foreign.mkdir()
        target.symlink_to(foreign, target_is_directory=True)
        self.install()
        self.assertEqual(target.resolve(), foreign)
        self.install("--force")
        self.assertEqual(target.resolve(), self.skill)
        self.assertTrue(foreign.exists())


if __name__ == "__main__":
    unittest.main()
