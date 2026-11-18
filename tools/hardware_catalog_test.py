import copy
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from jsonschema import ValidationError

from tools.hardware_catalog import (
    CATALOG_ROOT,
    MAX_RESPONSE_BYTES,
    accepts,
    check_history,
    generated_files,
    legacy_exports,
    load_catalog,
    query_catalog,
    read_json,
    validate_catalog,
    validate_query_report,
    validator_for,
)


class CatalogTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = load_catalog()
        cls.schema = read_json(CATALOG_ROOT / "schemas/catalog.schema.json")

    def changed(self):
        return copy.deepcopy(self.catalog)

    def test_existing_evidence_and_generated_exports_are_preserved(self):
        self.assertEqual(len(self.catalog["models"]), 5)
        self.assertEqual(len(self.catalog["runs"]), 13)
        self.assertEqual(
            sum(r["result"] == "failed" for r in self.catalog["runs"].values()), 4
        )
        for path, expected in generated_files(self.catalog).items():
            self.assertEqual(path.read_text(), expected, str(path))
        for run in self.catalog["runs"].values():
            self.assertIsNone(run["executed_by"])
            self.assertIsNone(run["started_at"])
            self.assertIsNone(run["validated_by"])
            self.assertIsNone(run["validated_at"])
            self.assertEqual(run["review_status"], "unreviewed")
        hardware, history = legacy_exports(self.catalog)
        self.assertEqual(len(hardware["hardware"]), 1)
        self.assertEqual(len(history["records"]), 2)
        self.assertEqual(history["records"][0]["results"]["measured_fps"], 8.46)
        self.assertEqual(history["records"][1]["results"]["received_frames"], 9001)
        self.assertEqual(
            len(history["records"][1]["results"]["ten_second_window_fps"]), 30
        )

    def test_templates_cover_all_device_kinds_and_record_types(self):
        templates = list((CATALOG_ROOT / "templates").glob("*.json"))
        self.assertEqual(len(templates), 14)
        kinds = set()
        for path in templates:
            entry = read_json(path)
            validator_for(self.schema, entry["record_type"]).validate(entry)
            if entry["record_type"] == "model":
                kinds.add(entry["kind"])
        self.assertEqual(len(kinds), 10)

    def test_invalid_category_fields_and_unknown_record_fields_are_rejected(self):
        model = copy.deepcopy(self.catalog["models"]["ti-lp-am243"])
        for mutate in (
            lambda m: m["category_details"].pop("mcu"),
            lambda m: m["category_details"].update(capture_modes=[]),
            lambda m: m.update(universal_verified=True),
        ):
            value = copy.deepcopy(model)
            mutate(value)
            with self.assertRaises(ValidationError):
                validator_for(self.schema, "model").validate(value)

    def test_unknown_references_and_versions_fail(self):
        for field, value in [
            ("hardware_id", "missing"),
            ("setup_id", "missing"),
            ("specification_id", "missing"),
            ("specification_version", 99),
        ]:
            catalog = self.changed()
            next(iter(catalog["runs"].values()))[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_catalog(catalog, check_files=False)

    def test_empty_or_blank_execution_and_validation_identities_are_rejected(self):
        for field in ("executed_by", "validated_by"):
            for username in ("", " ", "\t"):
                run = copy.deepcopy(next(iter(self.catalog["runs"].values())))
                run[field] = dict(
                    provider="github", username=username, organization=None
                )
                with self.subTest(field=field, username=username), self.assertRaises(
                    ValidationError
                ):
                    validator_for(self.schema, "run").validate(run)

    def test_review_requires_real_user_time_and_reference(self):
        for changes in [
            dict(
                validated_by={
                    "provider": "github",
                    "username": "reviewer",
                    "organization": None,
                }
            ),
            dict(review_status="reviewed"),
            dict(
                validated_by={
                    "provider": "github",
                    "username": "reviewer",
                    "organization": None,
                },
                validated_at="2026-11-18T12:00:00Z",
                review_reference="file:///tmp/review",
                review_status="reviewed",
            ),
        ]:
            catalog = self.changed()
            next(iter(catalog["runs"].values())).update(changes)
            with self.assertRaises(ValueError):
                validate_catalog(catalog, check_files=False)
        catalog = self.changed()
        run = next(iter(catalog["runs"].values()))
        run.update(
            validated_by={
                "provider": "github",
                "username": "reviewer",
                "organization": "Lab",
            },
            validated_at="2026-11-18T12:00:00Z",
            review_reference="https://github.com/example/project/pull/1#review",
            review_status="reviewed",
        )
        validate_catalog(catalog, check_files=False)

    def test_reversed_non_utc_and_precompletion_review_times_fail(self):
        cases = [
            ("2026-11-18T12:00:00Z", "2026-11-18T11:00:00Z"),
            ("2026-11-18T10:00:00", "2026-11-18T11:00:00Z"),
            ("2026-11-18T10:00:00+01:00", "2026-11-18T11:00:00Z"),
        ]
        for start, end in cases:
            catalog = self.changed()
            next(iter(catalog["runs"].values())).update(
                started_at=start, completed_at=end
            )
            with self.assertRaises(ValueError):
                validate_catalog(catalog, check_files=False)
        catalog = self.changed()
        next(iter(catalog["runs"].values())).update(
            started_at="2026-11-18T12:00:00Z",
            completed_at="2026-11-18T13:00:00Z",
            validated_at="2026-11-18T11:00:00Z",
            validated_by={
                "provider": "github",
                "username": "reviewer",
                "organization": None,
            },
            review_reference="https://github.com/example/project/pull/1",
            review_status="reviewed",
        )
        with self.assertRaises(ValueError):
            validate_catalog(catalog, check_files=False)

    def test_missing_historical_provenance_is_not_invented_or_silently_complete(self):
        catalog = self.changed()
        run = next(iter(catalog["runs"].values()))
        run["provenance"] = "complete"
        with self.assertRaises(ValueError):
            validate_catalog(catalog, check_files=False)
        catalog = self.changed()
        run = next(iter(catalog["runs"].values()))
        run["unknowns"].pop("executed_by")
        with self.assertRaises(ValueError):
            validate_catalog(catalog, check_files=False)

    def test_outcomes_cannot_claim_pass_for_failed_incomplete_or_unknown_measurements(
        self,
    ):
        for modify in ("overall", "observed", "missing", "unknown", "duplicate"):
            catalog = self.changed()
            run = catalog["runs"]["am243-soes-endurance-attempt-1-2026-10-26"]
            if modify == "overall":
                run["result"] = "passed"
            if modify == "observed":
                run["checks"][0]["observed"] = True
            if modify == "missing":
                run["checks"] = []
            if modify == "unknown":
                run["checks"][0]["check_id"] = "missing"
            if modify == "duplicate":
                run["checks"].append(copy.deepcopy(run["checks"][0]))
            with self.subTest(modify=modify), self.assertRaises(ValueError):
                validate_catalog(catalog, check_files=False)

    def test_measurement_comparisons_reject_bool_numeric_confusion(self):
        check = dict(operator="eq", expected=1)
        self.assertFalse(accepts(check, True))
        check.update(operator="gte", expected=30)
        self.assertFalse(accepts(check, None))
        self.assertFalse(accepts(check, "30"))
        self.assertTrue(accepts(check, 30.0))
        check.update(operator="between", expected=[1, 2])
        self.assertTrue(accepts(check, 1.5))
        check["expected"] = [2, 1]
        with self.assertRaises(ValueError):
            accepts(check, 1.5)

    def test_support_cannot_expand_without_review_or_passing_capability_evidence(self):
        catalog = self.changed()
        assessment = next(iter(catalog["assessments"].values()))
        assessment["status"] = "supported"
        with self.assertRaises(ValueError):
            validate_catalog(catalog, check_files=False)
        assessment.update(
            validated_at="2026-11-18T12:00:00Z",
            validated_by={
                "provider": "github",
                "username": "reviewer",
                "organization": None,
            },
            review_reference="https://github.com/example/project/pull/1",
            capabilities=["physical-stop"],
        )
        with self.assertRaises(ValueError):
            validate_catalog(catalog, check_files=False)

    def test_evidence_tampering_and_escaping_paths_fail(self):
        for field, value in [
            ("sha256", "0" * 64),
            ("path", "../outside.md"),
            ("path", "/tmp/outside.md"),
        ]:
            catalog = self.changed()
            next(iter(catalog["runs"].values()))["evidence"][0][field] = value
            with self.assertRaises(ValueError):
                validate_catalog(catalog)

    def test_duplicate_json_fields_and_nonfinite_values_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory, "record.json")
            for data in ('{"id":"a","id":"b"}', '{"x":NaN}', '{"x":Infinity}'):
                file.write_text(data)
                with self.assertRaises(ValueError):
                    read_json(file)

    def test_queries_are_paginated_stable_and_bind_cursors_to_filters(self):
        cursor = None
        ids = []
        while True:
            filters = dict(entity="runs", hardware_id="ti-lp-am243", limit=2)
            if cursor:
                filters["cursor"] = cursor
            report = query_catalog(**filters)
            validate_query_report(report)
            self.assertEqual(report["status_code"], "OK")
            ids.extend(e["id"] for e in report["data"]["entries"])
            cursor = report["data"]["next_cursor"]
            if not cursor:
                break
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(
            len(ids),
            sum(
                r["hardware_id"] == "ti-lp-am243" for r in self.catalog["runs"].values()
            ),
        )
        first = query_catalog(entity="runs", hardware_id="ti-lp-am243", limit=1)
        invalid = query_catalog(
            entity="runs",
            hardware_id="pjrc-teensy-41",
            cursor=first["data"]["next_cursor"],
        )
        self.assertEqual(invalid["status_code"], "INVALID_ARGUMENT")
        self.assertEqual(
            len(query_catalog(entity="runs", result="failed")["data"]["entries"]), 4
        )
        self.assertEqual(
            query_catalog(entity="runs", validated_by="unrecorded")["data"]["entries"],
            [],
        )
        self.assertEqual(
            query_catalog(entity="models", query="Arduino")["data"]["entries"][0][
                "integration"
            ]["status"],
            "unsupported",
        )

    def test_invalid_queries_return_bounded_explicit_errors(self):
        for filters in (
            {"limit": True},
            {"limit": 0},
            {"limit": "1"},
            {"limit": 51},
            {"entity": "../runs"},
            {"unknown": "a"},
            {"query": ""},
            {"query": "\x00"},
            {"entity": "models", "result": "passed"},
            {"cursor": "bad"},
        ):
            report = query_catalog(**filters)
            self.assertEqual(report["status_code"], "INVALID_ARGUMENT", report)
            validate_query_report(report)
            self.assertLess(len(json.dumps(report).encode()), MAX_RESPONSE_BYTES)
        self.assertEqual(
            query_catalog(hardware_id="unknown-model")["data"]["entries"], []
        )

    def test_response_and_catalog_admission_budgets_fail_explicitly(self):
        with patch("tools.hardware_catalog.MAX_RESPONSE_BYTES", 5000):
            report = query_catalog(entity="models", limit=50)
            self.assertEqual(report["status_code"], "OK")
            self.assertTrue(report["data"]["entries"])
            self.assertIsNotNone(report["data"]["next_cursor"])
            self.assertLessEqual(
                len(json.dumps(report, ensure_ascii=True).encode()), 5000
            )
        for name, value in [
            ("MAX_RESPONSE_BYTES", 100),
            ("MAX_CATALOG_BYTES", 100),
            ("MAX_RECORDS", 1),
            ("MAX_RECORD_BYTES", 100),
        ]:
            with patch("tools.hardware_catalog." + name, value):
                report = query_catalog()
                self.assertEqual(report["status_code"], "RESOURCE_EXHAUSTED", name)
                self.assertEqual(report["data"], {})

    def test_history_cannot_be_removed_or_rewritten_but_superseding_records_are_allowed(
        self,
    ):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

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
            folder = root / "docs/hardware_catalog/runs"
            folder.mkdir(parents=True)
            record = folder / "original.json"
            record.write_text('{"id":"original","result":"failed"}')
            model_dir = root / "docs/hardware_catalog/models"
            model_dir.mkdir()
            model = model_dir / "device.json"
            original_model = dict(
                id="device",
                kind="board",
                manufacturer="Vendor",
                model="Board",
                aliases=[],
            )
            model.write_text(json.dumps(original_model))
            git("add", ".")
            git("commit", "-m", "fixture")
            base = git("rev-parse", "HEAD")
            check_history(base, root)
            record.write_text('{"id":"original","result":"passed"}')
            with self.assertRaises(ValueError):
                check_history(base, root)
            record.unlink()
            with self.assertRaises(ValueError):
                check_history(base, root)
            record.write_text('{"result":"failed", "id":"original"}')
            (folder / "correction.json").write_text(
                '{"id":"correction","supersedes":"original"}'
            )
            check_history(base, root)
            updated = dict(original_model, aliases=["Alternate name"])
            model.write_text(json.dumps(updated))
            check_history(base, root)
            updated["model"] = "Different board"
            model.write_text(json.dumps(updated))
            with self.assertRaises(ValueError):
                check_history(base, root)
            model.unlink()
            with self.assertRaises(ValueError):
                check_history(base, root)
            with self.assertRaises(ValueError):
                check_history("--all", root)

    def test_scaffolding_never_overwrites_and_does_not_touch_devices(self):
        binary = (
            Path(os.environ["TEST_SRCDIR"])
            / os.environ["TEST_WORKSPACE"]
            / "tools/hardware_catalog_cli"
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory, "board.json")
            command = [
                str(binary),
                "--new",
                "model",
                "--kind",
                "board",
                "--id",
                "new-board",
                "--output",
                str(output),
            ]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            original = output.read_bytes()
            self.assertEqual(json.loads(original)["id"], "new-board")
            self.assertNotEqual(
                subprocess.run(command, capture_output=True).returncode, 0
            )
            self.assertEqual(output.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
