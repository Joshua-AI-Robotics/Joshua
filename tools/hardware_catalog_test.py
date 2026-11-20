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
    SOFTWARE_ROOT,
    check_history,
    export_catalog,
    legacy_exports,
    load_devices,
    load_software,
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
        cls.cli = (
            Path(os.environ["TEST_SRCDIR"])
            / os.environ["TEST_WORKSPACE"]
            / "tools/hardware_catalog_cli"
        )

    def camera(self):
        return copy.deepcopy(self.devices["sunplus-spca2281-xwf-1080p"])

    def test_one_file_contains_all_existing_history_and_inline_evidence(self):
        self.assertTrue(self.devices)
        self.assertTrue(load_software(check_files=False))
        migrated_ids = {
            "am243-soes-endurance-attempt-2-2026-10-26",
            "teensy41-tx-fault-2026-10-25",
            "am243-initial-rng-startup-failure-2026-10-26",
            "am243-soes-protocol-2026-10-26",
            "am243-vendor-ethercat-undated",
            "sunplus-spca2281-2026-11-04-humble-1080p-opt-300s",
            "am243-boot-identity-2026-10-26",
            "sunplus-spca2281-2026-11-04-humble-640x480",
            "am243-vendor-lrw-failure-undated",
            "am243-delayed-dispatch-2026-10-25",
            "am243-vendor-hello-world-undated",
            "am243-soes-endurance-attempt-1-2026-10-26",
            "teensy41-partial-write-2026-10-25",
        }
        tests = [
            t
            for d in self.devices.values()
            for t in d["validation_history"]
            if t["id"] in migrated_ids
        ]
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

    def test_unknown_candidate_capabilities_and_discovery_source_constraints(self):
        device = self.camera()
        device.pop("compatibility")
        device.update(capabilities=[], validation_history=[], support_history=[])
        device["integration"]["status"] = "candidate"
        Draft202012Validator(self.schema).validate(device)
        validate_device(device, check_files=False)
        device["integration"]["status"] = "integrated"
        with self.assertRaises(ValidationError):
            Draft202012Validator(self.schema).validate(device)
        device["integration"]["status"] = "candidate"
        device["discovery"]["source_url"] = "https://user:password@example.test/device"
        with self.assertRaisesRegex(ValueError, "without credentials"):
            validate_device(device, check_files=False)
        device["discovery"]["source_url"] = "https://example.test/device"
        device["discovery"]["coverage_exclusion"] = " "
        with self.assertRaisesRegex(ValueError, "nonblank reason"):
            validate_device(device, check_files=False)

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

    def test_support_claims_require_passing_checks_from_their_own_cited_runs(self):
        for status in ("supported", "limited"):
            for capability, run_id, accepted in (
                ("uart", "am243-vendor-hello-world-undated", True),
                ("ethercat", "am243-vendor-ethercat-undated", True),
                ("uart", "am243-vendor-ethercat-undated", False),
                ("uart", "am243-vendor-lrw-failure-undated", False),
                ("ethercat", "am243-vendor-lrw-failure-undated", False),
            ):
                with self.subTest(status=status, capability=capability, run_id=run_id):
                    device = copy.deepcopy(self.devices["ti-lp-am243"])
                    review = copy.deepcopy(device["support_history"][0])
                    review.update(
                        id="reviewed-uart",
                        status=status,
                        capabilities=["uart"],
                        run_ids=["am243-vendor-hello-world-undated"],
                        validated_by=dict(
                            provider="github", username="reviewer", organization=None
                        ),
                        validated_at="2026-11-18T12:00:00Z",
                        review_reference="https://github.com/example/project/pull/1",
                    )
                    device["support_history"].append(review)
                    claim = copy.deepcopy(review)
                    claim.update(
                        id="separate-claim",
                        capabilities=[capability],
                        run_ids=[run_id],
                    )
                    device["support_history"].append(claim)
                    Draft202012Validator(self.schema).validate(device)
                    if accepted:
                        validate_device(device, check_files=False)
                    else:
                        with self.assertRaisesRegex(ValueError, "cited runs"):
                            validate_device(device, check_files=False)

    def test_independent_reproduction_requires_distinct_provider_accounts(self):
        for provider, original_user, second_provider, second_user, accepted in (
            ("github", "SameAccount", "github", "SameAccount", False),
            ("github", "SameAccount", "github", "sameaccount", False),
            ("github", "SameAccount", "github", "another-account", True),
            ("other", "SameAccount", "other", "SameAccount", False),
            ("other", "SameAccount", "other", "sameaccount", True),
            ("github", "SameAccount", "other", "SameAccount", True),
        ):
            with self.subTest(
                provider=provider, second_provider=second_provider, user=second_user
            ):
                device = self.camera()
                original = device["validation_history"][0]
                original["executed_by"] = dict(
                    provider=provider, username=original_user, organization="Lab A"
                )
                reproduction = copy.deepcopy(original)
                reproduction.update(
                    id="reproduction",
                    reproduces=original["id"],
                    review_status="independently_reproduced",
                    executed_by=dict(
                        provider=second_provider,
                        username=second_user,
                        organization="Lab B",
                    ),
                    validated_by=dict(
                        provider="github", username="reviewer", organization=None
                    ),
                    validated_at="2026-11-18T12:00:00Z",
                    review_reference="https://github.com/example/project/pull/1",
                )
                device["validation_history"].append(reproduction)
                Draft202012Validator(self.schema).validate(device)
                if accepted:
                    validate_device(device, check_files=False)
                else:
                    with self.assertRaisesRegex(
                        ValueError, "distinct recorded operators"
                    ):
                        validate_device(device, check_files=False)

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
        self.assertTrue(result["data"]["entries"])
        self.assertLessEqual(len(result["data"]["entries"]), 20)
        self.assertTrue(
            all("validation_history" not in e for e in result["data"]["entries"])
        )
        self.assertEqual(
            query_catalog(kind="camera", hardware_id="sunplus-spca2281-xwf-1080p")[
                "data"
            ]["entries"][0]["id"],
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
        expected = {t["id"] for t in self.devices["ti-lp-am243"]["validation_history"]}
        self.assertEqual(set(seen), expected)
        self.assertEqual(len(seen), len(expected))
        failed = query_catalog(view="history", result="failed")
        validate_query_report(failed)
        self.assertTrue(failed["data"]["entries"])
        self.assertTrue(all(t["result"] == "failed" for t in failed["data"]["entries"]))
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
            self.assertEqual(
                len(load_devices(root, check_files=False)), len(self.devices) + 1
            )

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
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory, "new.json")
            args = [
                str(self.cli),
                "--new",
                "--id",
                "new-board",
                "--output",
                str(file),
            ]
            run = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            before = file.read_bytes()
            self.assertEqual(json.loads(before)["validation_history"], [])
            self.assertEqual(json.loads(before)["kind"], "board")
            self.assertNotEqual(subprocess.run(args, capture_output=True).returncode, 0)
            self.assertEqual(file.read_bytes(), before)

    def test_cli_queries_honor_explicit_kind_without_filtering_by_default(self):
        for view, kind in (
            ("devices", None),
            ("devices", "camera"),
            ("devices", "board"),
            ("devices", "actuator"),
            ("history", None),
            ("history", "camera"),
        ):
            with self.subTest(view=view, kind=kind):
                selected = [
                    d
                    for d in self.devices.values()
                    if kind is None or d["kind"] == kind
                ]
                count = (
                    len(selected)
                    if view == "devices"
                    else sum(len(d["validation_history"]) for d in selected)
                )
                args = [str(self.cli), "--query", view, "--limit", "50"]
                if kind:
                    args += ["--kind", kind]
                entries = []
                cursor = None
                while True:
                    run = subprocess.run(
                        args + (["--cursor", cursor] if cursor else []),
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(run.returncode, 0, run.stderr)
                    report = json.loads(run.stdout)
                    validate_query_report(report)
                    self.assertEqual(report["status_code"], "OK")
                    entries.extend(report["data"]["entries"])
                    cursor = report["data"]["next_cursor"]
                    if not cursor:
                        break
                self.assertEqual(len(entries), count)
                if kind:
                    for entry in entries:
                        actual_kind = (
                            entry["kind"]
                            if view == "devices"
                            else self.devices[entry["hardware_id"]]["kind"]
                        )
                        self.assertEqual(actual_kind, kind)


class CatalogExportTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.hardware = self.root / "docs/hardware_catalog"
        self.software = self.root / "docs/device_software_catalog"
        self.hardware.mkdir(parents=True)
        self.software.mkdir(parents=True)
        shutil.copy(CATALOG_ROOT / "catalog.schema.json", self.hardware)
        shutil.copy(SOFTWARE_ROOT / "catalog.schema.json", self.software)
        self.device = read_json(CATALOG_ROOT / "sunplus-spca2281-xwf-1080p.json")
        self.device.pop("compatibility")
        self.device.update(validation_history=[], support_history=[])
        self.device["integration"].update(
            implementations=["robot/camera.cc"],
            presets=["config/camera.pbtxt"],
            guide="docs/hardware_catalog/AGENTS.md",
        )
        self.library = read_json(SOFTWARE_ROOT / "opencv-opencv.json")
        self.library["integration"]["implementation"] = "robot/camera.cc"
        for name in (
            "robot/camera.cc",
            "config/camera.pbtxt",
            "docs/hardware_catalog/AGENTS.md",
        ):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n")
        self.write(self.hardware, self.device)
        self.write(self.software, self.library)
        self.git("init", "-q")
        self.git("config", "user.name", "Fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.commit()

    def git(self, *args):
        return subprocess.check_output(
            ["git", "-C", str(self.root), *args], text=True
        ).strip()

    def commit(self):
        self.git("add", ".")
        self.git("-c", "commit.gpgsign=false", "commit", "-qm", "fixture")

    def write(self, directory, record):
        (directory / (record["id"] + ".json")).write_text(json.dumps(record))

    def test_export_pins_sources_and_omits_validation_history(self):
        report = export_catalog(self.root)
        revision = self.git("rev-parse", "HEAD")
        self.assertEqual(report["joshua_revision"], revision)
        self.assertEqual(report["schema_version"], 1)
        self.assertEqual(len(report["hardware"]), 1)
        self.assertEqual(len(report["software"]), 1)
        self.assertEqual(
            report["totals"],
            {
                kind: dict(integrated=1, targets=1, percentage=100)
                for kind in ("hardware", "software")
            },
        )
        hardware, software = report["hardware"][0], report["software"][0]
        self.assertTrue(hardware["integrated"])
        self.assertTrue(software["integrated"])
        self.assertIn("/" + revision + "/", hardware["implementation_url"])
        self.assertIn(
            "/" + revision + "/robot/camera.cc", software["implementation_url"]
        )
        self.assertNotIn("validation_history", json.dumps(report))
        self.assertEqual(hardware["source_url"], self.device["discovery"]["source_url"])

    def test_adding_and_integrating_one_record_updates_export_and_coverage(self):
        added = copy.deepcopy(self.device)
        added.update(id="another-camera", aliases=["new-camera-alias"])
        added["integration"]["status"] = "candidate"
        self.write(self.hardware, added)
        with self.assertRaisesRegex(ValueError, "committed"):
            export_catalog(self.root)
        self.commit()
        report = export_catalog(self.root)
        self.assertEqual(len(report["hardware"]), 2)
        self.assertEqual(sum(e["integrated"] for e in report["hardware"]), 1)
        self.assertEqual(
            report["totals"]["hardware"], dict(integrated=1, targets=2, percentage=50)
        )
        added["integration"]["status"] = "integrated"
        self.write(self.hardware, added)
        self.commit()
        report = export_catalog(self.root)
        self.assertEqual(sum(e["integrated"] for e in report["hardware"]), 2)
        added["integration"]["status"] = "unsupported"
        added["discovery"]["coverage_exclusion"] = "Generic family already counted"
        self.write(self.hardware, added)
        self.commit()
        exported = export_catalog(self.root)
        entries = exported["hardware"]
        self.assertEqual(
            exported["totals"]["hardware"],
            dict(integrated=1, targets=1, percentage=100),
        )
        self.assertEqual(len(entries), 2)
        self.assertEqual(sum(e["counts_toward_coverage"] for e in entries), 1)
        self.assertEqual(entries[0]["aliases"], ["new-camera-alias"])

    def test_dirty_sources_and_missing_implementations_cannot_publish(self):
        implementation = self.root / "robot/camera.cc"
        implementation.write_text("changed\n")
        with self.assertRaisesRegex(ValueError, "committed"):
            export_catalog(self.root)
        implementation.unlink()
        self.commit()
        with self.assertRaisesRegex(ValueError, "Missing"):
            export_catalog(self.root)

    def test_duplicate_software_sources_and_invalid_discovery_fail(self):
        duplicate = copy.deepcopy(self.library)
        duplicate["id"] = "duplicate-library"
        self.write(self.software, duplicate)
        self.commit()
        with self.assertRaisesRegex(ValueError, "unique implementation"):
            export_catalog(self.root)
        (self.software / "duplicate-library.json").unlink()
        self.library["category"] = "Simulation"
        self.write(self.software, self.library)
        self.commit()
        with self.assertRaises(ValueError):
            export_catalog(self.root)
        self.library["category"] = "Sensor capture"
        self.library["source_url"] = "https://user:password@example.test/library"
        self.write(self.software, self.library)
        self.commit()
        with self.assertRaisesRegex(ValueError, "without credentials"):
            export_catalog(self.root)


if __name__ == "__main__":
    unittest.main()
