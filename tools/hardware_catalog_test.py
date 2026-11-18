import copy
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from jsonschema import Draft202012Validator, ValidationError

from tools.hardware_catalog import (
    CATALOG_ROOT,
    check_history,
    legacy_exports,
    load_devices,
    query_catalog,
    read_json,
    validate_device,
    validate_query_report,
)


class DeviceCatalogTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.devices = load_devices(check_files=False)
        cls.schema = read_json(CATALOG_ROOT / "catalog.schema.json")

    def camera(self):
        return copy.deepcopy(self.devices["sunplus-spca2281-xwf-1080p"])

    def test_one_file_contains_all_existing_history_and_inline_evidence(self):
        self.assertEqual(len(self.devices), 5)
        tests = [t for d in self.devices.values() for t in d["validation_history"]]
        self.assertEqual(len(tests), 13)
        self.assertEqual(sum(t["result"] == "failed" for t in tests), 4)
        self.assertFalse(any(p.is_dir() for p in CATALOG_ROOT.iterdir()))
        for test in tests:
            self.assertIsNone(test["executed_by"])
            self.assertIsNone(test["validated_by"])
            self.assertIn("setup", test)
            self.assertIn("specification", test)
            for e in test["evidence"]:
                self.assertEqual(e["availability"], "inline")
                self.assertEqual(
                    hashlib.sha256(e["notes"].encode()).hexdigest(), e["sha256"]
                )

    def test_compatibility_views_preserve_original_measurements_and_order(self):
        models, runs = legacy_exports(self.devices)
        self.assertEqual(len(models["hardware"]), 1)
        self.assertEqual(len(runs["records"]), 2)
        self.assertEqual(runs["records"][0]["results"]["measured_fps"], 8.46)
        self.assertEqual(runs["records"][1]["results"]["received_frames"], 9001)
        self.assertEqual(
            len(runs["records"][1]["results"]["ten_second_window_fps"]), 30
        )
        self.assertEqual(
            runs["records"][0]["evidence"],
            "docs/hardware_catalog/sunplus-spca2281-xwf-1080p.json",
        )

    def test_blank_tester_reviewer_and_wrong_device_fields_fail_schema(self):
        for field in ("executed_by", "validated_by"):
            for user in ("", " ", "\t"):
                d = self.camera()
                d["validation_history"][0][field] = dict(
                    provider="github", username=user, organization=None
                )
                with self.assertRaises(ValidationError):
                    Draft202012Validator(self.schema).validate(d)
        d = self.camera()
        d["category_details"]["mcu"] = "wrong kind"
        with self.assertRaises(ValidationError):
            Draft202012Validator(self.schema).validate(d)

    def test_missing_review_identity_time_reference_or_reversed_times_fail(self):
        for changes in (
            dict(review_status="reviewed"),
            dict(validated_at="2026-11-18T12:00:00Z"),
            dict(
                started_at="2026-11-18T12:00:00Z", completed_at="2026-11-18T11:00:00Z"
            ),
            dict(
                started_at="2026-11-18T12:00:00+01:00",
                completed_at="2026-11-18T13:00:00Z",
            ),
        ):
            d = self.camera()
            d["validation_history"][0].update(changes)
            with self.assertRaises(ValueError):
                validate_device(d, check_files=False)
        d = self.camera()
        d["validation_history"][0].update(
            review_status="reviewed",
            validated_by=dict(
                provider="github", username="reviewer", organization=None
            ),
            validated_at="2026-11-18T12:00:00Z",
            review_reference="https://github.com/example/project/pull/1",
        )
        validate_device(d, check_files=False)

    def test_unknown_provenance_and_false_passes_fail(self):
        for mutation in (
            "unknown",
            "complete",
            "observed",
            "overall",
            "duplicate",
            "missing",
        ):
            d = self.camera()
            test = d["validation_history"][0]
            if mutation == "unknown":
                test["unknowns"].pop("executed_by")
            if mutation == "complete":
                test["provenance"] = "complete"
            if mutation == "observed":
                test["checks"][0]["observed"] = False
            if mutation == "overall":
                test["result"] = "failed"
            if mutation == "duplicate":
                d["validation_history"].append(copy.deepcopy(test))
            if mutation == "missing":
                test["checks"] = []
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                validate_device(d, check_files=False)

    def test_inline_evidence_tampering_and_unknown_setup_subject_fail(self):
        d = self.camera()
        d["validation_history"][0]["evidence"][0]["notes"] += "changed"
        with self.assertRaises(ValueError):
            validate_device(d, check_files=False)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            shutil.copytree(CATALOG_ROOT, target, dirs_exist_ok=True)
            p = target / "sunplus-spca2281-xwf-1080p.json"
            d = self.camera()
            d["validation_history"][0]["setup"]["subjects"][0][
                "hardware_id"
            ] = "missing"
            p.write_text(json.dumps(d))
            with self.assertRaises(ValueError):
                load_devices(target, check_files=False)

    def test_queries_are_device_first_and_history_paginates(self):
        result = query_catalog()
        validate_query_report(result)
        self.assertEqual(len(result["data"]["entries"]), 5)
        self.assertNotIn("notes", json.dumps(result["data"]["entries"]))
        self.assertEqual(
            query_catalog(kind="camera")["data"]["entries"][0]["id"],
            "sunplus-spca2281-xwf-1080p",
        )
        seen = []
        cursor = None
        while True:
            filters = dict(view="history", hardware_id="ti-lp-am243", limit=2)
            if cursor:
                filters["cursor"] = cursor
            page = query_catalog(**filters)
            validate_query_report(page)
            self.assertEqual(page["status_code"], "OK")
            seen += [t["id"] for t in page["data"]["entries"]]
            cursor = page["data"]["next_cursor"]
            if not cursor:
                break
        self.assertEqual(len(seen), 9)
        self.assertEqual(len(set(seen)), 9)
        self.assertEqual(
            len(query_catalog(view="history", result="failed")["data"]["entries"]), 4
        )
        self.assertEqual(
            query_catalog(view="history", validated_by="unknown")["data"]["entries"], []
        )

    def test_invalid_filters_stale_cursors_and_budgets_return_explicit_errors(self):
        for filters in (
            {"limit": True},
            {"limit": 0},
            {"limit": "1"},
            {"view": "runs"},
            {"entity": "models"},
            {"query": " "},
            {"query": "\x00"},
            {"result": "passed"},
            {"cursor": "bad"},
        ):
            result = query_catalog(**filters)
            self.assertEqual(result["status_code"], "INVALID_ARGUMENT")
            validate_query_report(result)
        first = query_catalog(view="history", hardware_id="ti-lp-am243", limit=1)
        self.assertEqual(
            query_catalog(
                view="history",
                hardware_id="pjrc-teensy-41",
                cursor=first["data"]["next_cursor"],
            )["status_code"],
            "INVALID_ARGUMENT",
        )
        for key, value in [
            ("MAX_RESPONSE_BYTES", 100),
            ("MAX_RECORD_BYTES", 100),
            ("MAX_RECORDS", 1),
            ("MAX_CATALOG_BYTES", 100),
        ]:
            with patch("tools.hardware_catalog." + key, value):
                self.assertEqual(query_catalog()["status_code"], "RESOURCE_EXHAUSTED")

    def test_adding_one_file_is_discovered_without_other_metadata_updates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(CATALOG_ROOT, root, dirs_exist_ok=True)
            d = self.camera()
            d.update(
                id="another-camera",
                model="Another camera",
                validation_history=[],
                support_history=[],
            )
            d.pop("compatibility", None)
            (root / "another-camera.json").write_text(json.dumps(d))
            self.assertEqual(len(load_devices(root, check_files=False)), 6)

    def test_history_allows_append_and_integration_notes_but_preserves_past_tests(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = root / "docs/hardware_catalog"
            folder.mkdir(parents=True)
            device = self.camera()
            file = folder / (device["id"] + ".json")
            file.write_text(json.dumps(device))

            def git(*args):
                return subprocess.run(
                    ["git", "-C", str(root), *args],
                    capture_output=True,
                    text=True,
                    check=True,
                ).stdout.strip()

            git("init")
            git("config", "user.name", "Fixture")
            git("config", "user.email", "fixture@example.invalid")
            git("add", ".")
            git("commit", "-m", "fixture")
            base = git("rev-parse", "HEAD")
            device["integration"]["support_scope"] = "Updated current integration notes"
            added = copy.deepcopy(device["validation_history"][0])
            added["id"] = "new-test"
            device["validation_history"].append(added)
            file.write_text(json.dumps(device))
            check_history(base, root)
            device["validation_history"][0]["result"] = "failed"
            file.write_text(json.dumps(device))
            with self.assertRaises(ValueError):
                check_history(base, root)
            file.unlink()
            with self.assertRaises(ValueError):
                check_history(base, root)

    def test_scaffold_creates_one_device_and_never_overwrites(self):
        binary = (
            Path(os.environ["TEST_SRCDIR"])
            / os.environ["TEST_WORKSPACE"]
            / "tools/hardware_catalog_cli"
        )
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory, "new.json")
            args = [
                str(binary),
                "--new",
                "--kind",
                "board",
                "--id",
                "new-board",
                "--output",
                str(file),
            ]
            run = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            before = file.read_bytes()
            self.assertEqual(json.loads(before)["validation_history"], [])
            self.assertNotEqual(subprocess.run(args, capture_output=True).returncode, 0)
            self.assertEqual(file.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
