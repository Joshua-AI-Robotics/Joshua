"""Independent model discovery and linked historical hardware test records."""

import copy
import json
import tempfile
import unittest
from pathlib import Path

from tools.mcp.hardware_catalog import (
    CATALOG_GUIDE_PATH,
    CATALOG_PATH,
    DATA_KEYS,
    MODEL_OPERATION,
    RECORDS_PATH,
    TEST_GUIDE_PATH,
    TEST_OPERATION,
    HardwareCatalog,
    hardware_error,
    load_hardware,
    load_test_records,
    render_catalog_markdown,
    render_test_markdown,
    validate_hardware,
    validate_hardware_report,
    validate_references,
    validate_test_links,
    validate_test_record,
)


def write_metadata(directory, hardware=None, records=None):
    catalog, tests = Path(directory, "catalog.json"), Path(directory, "tests.json")
    catalog.write_text(
        json.dumps(
            dict(
                schema_version=1,
                hardware=load_hardware() if hardware is None else hardware,
            )
        )
    )
    tests.write_text(
        json.dumps(
            dict(
                schema_version=1,
                records=load_test_records() if records is None else records,
            )
        )
    )
    return catalog, tests


class CatalogIntegrityTest(unittest.TestCase):
    def test_bundled_metadata_links_and_generated_guides(self):
        hardware, records = load_hardware(), load_test_records()
        self.assertTrue(hardware)
        self.assertTrue(records)
        validate_references(hardware, records)
        self.assertEqual(
            CATALOG_GUIDE_PATH.read_text(), render_catalog_markdown(hardware)
        )
        self.assertEqual(
            TEST_GUIDE_PATH.read_text(), render_test_markdown(hardware, records)
        )
        self.assertNotIn("measured_fps", hardware[0])
        self.assertNotIn("verification", hardware[0])
        self.assertNotIn("model", records[0])
        self.assertNotIn("driver", records[0])

    def test_model_and_test_schemas_reject_mixed_fields_and_bad_provenance(self):
        hardware, record = load_hardware()[0], load_test_records()[0]
        for field, value in (
            ("support_scope", ""),
            ("preset", "/dev/video0"),
            ("identifiers", {"usb_vid_pid": "not-a-usb-id"}),
            ("aliases", "SPCA2281"),
            ("verification", record),
        ):
            bad = copy.deepcopy(hardware)
            bad[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_hardware(bad)
        for field, value in (
            ("hardware_id", "../outside"),
            ("date", "2026-02-30"),
            ("joshua_commit", "HEAD"),
            ("evidence", "../outside.md"),
            ("environment", {}),
            ("results", {"fps": float("nan")}),
            ("limitations", []),
            ("model", hardware["model"]),
        ):
            bad = copy.deepcopy(record)
            bad[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_test_record(bad)

    def test_duplicate_model_test_and_json_field_ids_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            for key, entries, loader in (
                ("hardware", load_hardware(), load_hardware),
                ("records", load_test_records(), load_test_records),
            ):
                path = Path(directory, "duplicate.json")
                path.write_text(json.dumps({"schema_version": 1, key: entries * 2}))
                with self.subTest(key=key), self.assertRaisesRegex(
                    ValueError, "duplicate"
                ):
                    loader(path)
                path.write_text(
                    '{"schema_version": 1, "schema_version": 1, "' + key + '": []}'
                )
                with self.assertRaisesRegex(ValueError, "duplicate"):
                    loader(path)

    def test_unknown_hardware_and_missing_source_preset_evidence_are_rejected(self):
        hardware, records = load_hardware(), load_test_records()
        bad = copy.deepcopy(records)
        bad[0]["hardware_id"] = "unknown-model"
        with self.assertRaisesRegex(ValueError, "unknown hardware_id"):
            validate_test_links(hardware, bad)
        for field in ("implementation", "preset", "evidence"):
            models, runs = copy.deepcopy(hardware), copy.deepcopy(records)
            (runs if field == "evidence" else models)[0][field] = "docs/missing.md"
            with self.subTest(field=field), self.assertRaisesRegex(
                ValueError, "Missing"
            ):
                validate_references(models, runs)


class HardwareDiscoveryTest(unittest.IsolatedAsyncioTestCase):
    async def test_model_alias_matching_and_combined_filters(self):
        backend = HardwareCatalog()
        for filters in (
            {},
            {"kind": "camera"},
            {"query": "xwf-1080p"},
            {"query": " SPCA2281 "},
            {"query": "Sunplus"},
            {"kind": "camera", "query": "XWF"},
        ):
            with self.subTest(filters=filters):
                report = await backend.list_supported_hardware(**filters)
                validate_hardware_report(report, MODEL_OPERATION)
                self.assertEqual(report["status_code"], "OK")
                self.assertEqual(len(report["data"]["hardware"]), 1)
        for filters in (
            {"kind": "motor", "query": "XWF"},
            {"query": "unknown-camera"},
            {"usb_id": "0000:0000"},
        ):
            report = await backend.list_supported_hardware(**filters)
            self.assertEqual(report["status_code"], "OK")
            self.assertEqual(report["data"]["hardware"], [])

    async def test_usb_matching_uses_only_synthetic_model_metadata(self):
        model = copy.deepcopy(load_hardware()[0])
        model.update(
            id="fixture-camera",
            manufacturer="Fixture vendor",
            model="Fixture camera",
            aliases=[],
            identifiers={"usb_vid_pid": "abcd:0123"},
        )
        with tempfile.TemporaryDirectory() as directory:
            catalog, tests = write_metadata(directory, hardware=[model], records=[])
            backend = HardwareCatalog(catalog, tests)
            for filters in (
                {"usb_id": "ABCD:0123"},
                {"kind": "camera", "usb_id": "abcd:0123", "query": "Fixture"},
            ):
                report = await backend.list_supported_hardware(**filters)
                self.assertEqual(report["data"]["hardware"][0]["id"], "fixture-camera")
            report = await backend.list_supported_hardware(
                kind="motor", usb_id="abcd:0123"
            )
            self.assertEqual(report["data"]["hardware"], [])

    async def test_models_with_zero_and_multiple_runs_have_independent_support_scope(
        self,
    ):
        hardware, records = load_hardware(), load_test_records()
        model = copy.deepcopy(hardware[0])
        model.update(
            id="fixture-motor", kind="motor", model="Fixture motor", aliases=[]
        )
        hardware.append(model)
        second = copy.deepcopy(records[0])
        second.update(id="second-run", environment={"os": "Fixture OS"})
        records.append(second)
        with tempfile.TemporaryDirectory() as directory:
            catalog, tests = write_metadata(directory, hardware, records)
            backend = HardwareCatalog(catalog, tests)
            report = await backend.list_hardware_test_records(hardware[0]["id"])
            validate_hardware_report(report, TEST_OPERATION)
            self.assertEqual(len(report["data"]["records"]), 2)
            self.assertNotIn("model", report["data"]["records"][0])
            report = await backend.list_hardware_test_records("fixture-motor")
            self.assertEqual(report["data"]["records"], [])
            report = await backend.list_supported_hardware(kind="motor")
            self.assertEqual(report["data"]["hardware"], [model])
            # A new run changes history only, with no automatic support promotion.
            before = await backend.list_supported_hardware()
            records.append(
                {**second, "id": "third-run", "hardware_id": "fixture-motor"}
            )
            tests.write_text(json.dumps(dict(schema_version=1, records=records)))
            self.assertEqual(await backend.list_supported_hardware(), before)
            report = await backend.list_hardware_test_records("fixture-motor")
            self.assertEqual(len(report["data"]["records"]), 1)

    async def test_invalid_filters_and_unknown_hardware(self):
        backend = HardwareCatalog()
        for filters in (
            {"kind": []},
            {"kind": "../camera"},
            {"usb_id": "abcd"},
            {"usb_id": 1234},
            {"query": " "},
            {"query": "x" * 257},
            {"query": "\x00"},
            {"query": []},
            {"query": "\ud800"},
        ):
            report = await backend.list_supported_hardware(**filters)
            validate_hardware_report(report, MODEL_OPERATION)
            self.assertEqual(report["status_code"], "INVALID_ARGUMENT")
        for hardware_id in ([], "../outside", "", "\ud800"):
            report = await backend.list_hardware_test_records(hardware_id)
            validate_hardware_report(report, TEST_OPERATION)
            self.assertEqual(report["status_code"], "INVALID_ARGUMENT")
        report = await backend.list_hardware_test_records("unknown-model")
        self.assertEqual(report["status_code"], "OK")
        self.assertEqual(report["data"]["records"], [])

    async def test_model_discovery_does_not_depend_on_available_test_history(self):
        with tempfile.TemporaryDirectory() as directory:
            catalog, tests = write_metadata(directory)
            backend = HardwareCatalog(catalog, tests)
            tests.unlink()
            report = await backend.list_supported_hardware()
            self.assertEqual(report["status_code"], "OK")
            report = await backend.list_hardware_test_records()
            self.assertEqual(report["status_code"], "UNAVAILABLE")
            tests.write_text("{")
            report = await backend.list_supported_hardware()
            self.assertEqual(report["status_code"], "OK")
            report = await backend.list_hardware_test_records()
            self.assertEqual(report["status_code"], "DATA_LOSS")
            tests.write_bytes(RECORDS_PATH.read_bytes())
            self.assertEqual(
                (await backend.list_hardware_test_records())["status_code"], "OK"
            )

    async def test_malformed_oversized_and_missing_files_return_bounded_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            catalog, tests = write_metadata(directory)
            backend = HardwareCatalog(catalog, tests)
            for path, operation, original in (
                (catalog, MODEL_OPERATION, CATALOG_PATH),
                (tests, TEST_OPERATION, RECORDS_PATH),
            ):
                path.unlink()
                for raw, code in (
                    (None, "UNAVAILABLE"),
                    (b"{", "DATA_LOSS"),
                    (b"x" * 65537, "RESOURCE_EXHAUSTED"),
                ):
                    if raw is not None:
                        path.write_bytes(raw)
                    report = await getattr(backend, operation)()
                    validate_hardware_report(report, operation)
                    self.assertEqual(report["status_code"], code)
                path.write_bytes(original.read_bytes())
            records = load_test_records()
            records[0]["hardware_id"] = "orphan-model"
            tests.write_text(json.dumps(dict(schema_version=1, records=records)))
            report = await backend.list_hardware_test_records()
            self.assertEqual(report["status_code"], "DATA_LOSS")

    async def test_both_reports_bound_unicode_expansion(self):
        for operation in (MODEL_OPERATION, TEST_OPERATION):
            hardware, records = load_hardware(), load_test_records()
            if operation == MODEL_OPERATION:
                hardware[0]["aliases"] = ["\u6f22" * 600] * 32
            else:
                records[0]["results"] = {"measurement": "\u6f22" * 12000}
            with tempfile.TemporaryDirectory() as directory:
                catalog, tests = write_metadata(directory)
                path = catalog if operation == MODEL_OPERATION else tests
                path.write_text(
                    json.dumps(
                        {
                            "schema_version": 1,
                            DATA_KEYS[operation]: (
                                hardware if operation == MODEL_OPERATION else records
                            ),
                        },
                        ensure_ascii=False,
                    ),
                    encoding="utf-8",
                )
                self.assertLess(path.stat().st_size, 65536)
                report = await getattr(HardwareCatalog(catalog, tests), operation)()
                validate_hardware_report(report, operation)
                self.assertEqual(report["status_code"], "RESOURCE_EXHAUSTED")
                self.assertLess(len(json.dumps(report).encode()), 65536)

    async def test_reports_cannot_mix_model_and_test_schemas_or_claim_readiness(self):
        backend = HardwareCatalog()
        report = await backend.list_supported_hardware()
        with self.assertRaises(ValueError):
            validate_hardware_report(report, TEST_OPERATION)
        for operation in (MODEL_OPERATION, TEST_OPERATION):
            report = hardware_error(operation, "DATA_LOSS", "fixture")
            report["limitations"] = ["The camera is ready to run."]
            with self.assertRaises(ValueError):
                validate_hardware_report(report, operation)


if __name__ == "__main__":
    unittest.main()
