"""Separate model support metadata and historical tests; no runtime access."""

import argparse
import datetime
import json
import re
from pathlib import Path, PurePosixPath

from tools.hardware_catalog import (
    QUERY_OPERATION,
    QUERY_REPORT_SCHEMA,
    query_catalog,
    query_error,
    validate_query_report,
)

ROOT = Path(__file__).parents[2]
CATALOG_PATH = ROOT / "docs/hardware_catalog.json"
RECORDS_PATH = ROOT / "docs/hardware_test_records.json"
CATALOG_GUIDE_PATH = ROOT / "docs/SUPPORTED_HARDWARE.md"
TEST_GUIDE_PATH = ROOT / "docs/TESTED_HARDWARE.md"
MAX_BYTES = 65536
MODEL_OPERATION = "list_supported_hardware"
TEST_OPERATION = "list_hardware_test_records"
SCOPES = {
    MODEL_OPERATION: "hardware_catalog",
    TEST_OPERATION: "hardware_test_history",
}
SOURCES = {
    MODEL_OPERATION: "docs/hardware_catalog.json",
    TEST_OPERATION: "docs/hardware_test_records.json",
}
DATA_KEYS = {MODEL_OPERATION: "hardware", TEST_OPERATION: "records"}
LIMITATIONS = {
    MODEL_OPERATION: [
        "Model entries describe intended support through existing implementations "
        "and presets. They do not establish current connectivity, hardware readiness, "
        "or compatibility of every revision. Historical test evidence is separate. "
        "No config, driver, ROS, or device was opened."
    ],
    TEST_OPERATION: [
        "These are historical tests on specific hardware and software setups. "
        "A test does not automatically broaden the model's support scope or establish "
        "current connectivity, hardware readiness, or permission to run. "
        "No config, driver, ROS, or device was opened."
    ],
}
STATUS_CODES = [
    "OK",
    "INVALID_ARGUMENT",
    "UNAVAILABLE",
    "DATA_LOSS",
    "RESOURCE_EXHAUSTED",
]


def _report_schema(operation):
    properties = {
        "schema_version": {"type": "integer", "const": 1},
        "scope": {"type": "string", "const": SCOPES[operation]},
        "operation": {"type": "string", "const": operation},
        "status_code": {"type": "string", "enum": STATUS_CODES},
        "message": {"type": "string"},
        "data": {"type": "object"},
        "limitations": {"type": "array", "items": {"type": "string"}},
    }
    return {
        "type": "object",
        "properties": properties,
        "required": sorted(properties),
        "additionalProperties": False,
    }


REPORT_SCHEMAS = {op: _report_schema(op) for op in SCOPES}
REPORT_SCHEMAS[QUERY_OPERATION] = QUERY_REPORT_SCHEMA


def _unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate catalog field")
        result[key] = value
    return result


def _text(value):
    if type(value) is not str or not value.strip():
        return False
    try:
        return len(value.encode("utf-8")) <= 2048 and not any(
            ord(c) < 32 for c in value
        )
    except UnicodeEncodeError:
        return False


def _identity(value):
    return (
        type(value) is str
        and re.fullmatch(r"[a-z][a-z0-9_-]{0,127}", value) is not None
    )


def _reference(value):
    return (
        _text(value)
        and not PurePosixPath(value).is_absolute()
        and all(part not in {"", ".", ".."} for part in value.split("/"))
        and "\\" not in value
    )


def validate_hardware(hardware):
    if type(hardware) is not dict or set(hardware) != {
        "id",
        "kind",
        "manufacturer",
        "model",
        "aliases",
        "identifiers",
        "driver",
        "implementation",
        "preset",
        "support_scope",
    }:
        raise ValueError("wrong hardware model fields")
    if (
        not _identity(hardware["id"])
        or not _identity(hardware["kind"])
        or any(
            not _text(hardware[key])
            for key in ("manufacturer", "model", "driver", "support_scope")
        )
    ):
        raise ValueError("invalid hardware identity or support scope")
    if (
        type(hardware["aliases"]) is not list
        or len(hardware["aliases"]) > 32
        or any(not _text(alias) for alias in hardware["aliases"])
        or type(hardware["identifiers"]) is not dict
        or len(hardware["identifiers"]) > 16
        or any(not _text(k) or not _text(v) for k, v in hardware["identifiers"].items())
    ):
        raise ValueError("invalid hardware aliases or identifiers")
    usb_id = hardware["identifiers"].get("usb_vid_pid")
    if usb_id is not None and not re.fullmatch(r"[0-9a-f]{4}:[0-9a-f]{4}", usb_id):
        raise ValueError("invalid USB VID:PID")
    if not _reference(hardware["implementation"]) or not _reference(hardware["preset"]):
        raise ValueError("invalid implementation or preset reference")


def validate_test_record(record):
    if type(record) is not dict or set(record) != {
        "id",
        "hardware_id",
        "date",
        "joshua_commit",
        "environment",
        "evidence",
        "results",
        "limitations",
    }:
        raise ValueError("wrong hardware test fields")
    if (
        not _identity(record["id"])
        or not _identity(record["hardware_id"])
        or not _text(record["date"])
        or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", record["date"])
        or not _text(record["joshua_commit"])
        or not re.fullmatch(r"[0-9a-f]{40}", record["joshua_commit"])
        or not _reference(record["evidence"])
    ):
        raise ValueError("invalid test identity or provenance")
    datetime.date.fromisoformat(record["date"])
    environment, limitations = record["environment"], record["limitations"]
    if (
        type(environment) is not dict
        or not environment
        or any(not _text(k) or not _text(v) for k, v in environment.items())
        or type(record["results"]) is not dict
        or not record["results"]
        or type(limitations) is not list
        or not limitations
        or any(not _text(item) for item in limitations)
    ):
        raise ValueError("invalid test environment, results, or limitations")
    json.dumps(record, allow_nan=False)


def _load_entries(path, key, validator):
    with path.open("rb") as stream:
        raw = stream.read(MAX_BYTES + 1)
    if len(raw) > MAX_BYTES:
        raise OverflowError("Hardware metadata exceeds 64 KiB.")
    data = json.loads(raw.decode("utf-8"), object_pairs_hook=_unique_pairs)
    if (
        type(data) is not dict
        or set(data) != {"schema_version", key}
        or type(data["schema_version"]) is not int
        or data["schema_version"] != 1
        or type(data[key]) is not list
        or len(data[key]) > 128
    ):
        raise ValueError("invalid hardware metadata")
    identities = set()
    for entry in data[key]:
        validator(entry)
        if entry["id"] in identities:
            raise ValueError("duplicate hardware or test identity")
        identities.add(entry["id"])
    return data[key]


def load_hardware(path=CATALOG_PATH):
    return _load_entries(path, "hardware", validate_hardware)


def load_test_records(path=RECORDS_PATH):
    return _load_entries(path, "records", validate_test_record)


def validate_test_links(hardware, records):
    identities = {entry["id"] for entry in hardware}
    for record in records:
        if record["hardware_id"] not in identities:
            raise ValueError("test references an unknown hardware_id")


def validate_references(hardware, records, root=ROOT):
    validate_test_links(hardware, records)
    references = [
        item[key] for item in hardware for key in ("implementation", "preset")
    ]
    references.extend(record["evidence"] for record in records)
    for reference in references:
        if not (root / reference).is_file():
            raise ValueError(f"Missing hardware metadata reference: {reference}")


def hardware_error(operation, code, message):
    if operation == QUERY_OPERATION:
        return query_error(code, message)
    return dict(
        schema_version=1,
        scope=SCOPES[operation],
        operation=operation,
        status_code=code,
        message=message,
        data={},
        limitations=list(LIMITATIONS[operation]),
    )


def validate_hardware_report(report, operation):
    if operation == QUERY_OPERATION:
        validate_query_report(report)
        return
    if (
        type(report) is not dict
        or set(report) != set(REPORT_SCHEMAS[operation]["properties"])
        or type(report["schema_version"]) is not int
        or report["schema_version"] != 1
        or report["scope"] != SCOPES[operation]
        or report["operation"] != operation
        or report["status_code"] not in STATUS_CODES
        or not _text(report["message"])
        or report["limitations"] != LIMITATIONS[operation]
        or type(report["data"]) is not dict
    ):
        raise ValueError("invalid hardware metadata report")
    data, key = report["data"], DATA_KEYS[operation]
    if report["status_code"] == "OK":
        if (
            set(data) != {key, "source"}
            or data["source"] != SOURCES[operation]
            or type(data[key]) is not list
            or len(data[key]) > 128
        ):
            raise ValueError("invalid hardware metadata data")
        validator = (
            validate_hardware if operation == MODEL_OPERATION else validate_test_record
        )
        for item in data[key]:
            validator(item)
    elif data:
        raise ValueError("failed hardware query must not have data")


def _read_error(operation, error):
    if isinstance(error, OSError):
        code, message = "UNAVAILABLE", "Bundled hardware metadata unavailable."
    elif isinstance(error, OverflowError):
        code, message = "RESOURCE_EXHAUSTED", "Hardware metadata exceeds 64 KiB."
    else:
        code, message = "DATA_LOSS", "Bundled hardware metadata is malformed."
    return hardware_error(operation, code, message)


def _success(operation, entries):
    report = hardware_error(operation, "OK", "Current hardware has not been assessed.")
    report["data"] = {DATA_KEYS[operation]: entries, "source": SOURCES[operation]}
    if len(json.dumps(report, ensure_ascii=True, allow_nan=False).encode()) > MAX_BYTES:
        return hardware_error(
            operation,
            "RESOURCE_EXHAUSTED",
            "Hardware report exceeds 64 KiB; narrow the filters.",
        )
    return report


class HardwareCatalog:
    def __init__(self, catalog_path=CATALOG_PATH, records_path=RECORDS_PATH):
        self.catalog_path, self.records_path = catalog_path, records_path

    async def query_hardware_catalog(self, **filters):
        return query_catalog(**filters)

    async def list_supported_hardware(self, kind=None, usb_id=None, query=None):
        if (
            kind is not None
            and not _identity(kind)
            or usb_id is not None
            and (
                type(usb_id) is not str
                or re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{4}", usb_id) is None
            )
            or query is not None
            and (not _text(query) or len(query) > 256)
        ):
            return hardware_error(
                MODEL_OPERATION, "INVALID_ARGUMENT", "Invalid model filter."
            )
        try:
            hardware = load_hardware(self.catalog_path)
        except (OSError, OverflowError, ValueError, TypeError, RecursionError) as error:
            return _read_error(MODEL_OPERATION, error)
        matches = []
        for item in hardware:
            if kind is not None and item["kind"] != kind:
                continue
            if (
                usb_id is not None
                and item["identifiers"].get("usb_vid_pid") != usb_id.lower()
            ):
                continue
            names = [item[key] for key in ("id", "manufacturer", "model", "driver")]
            names.extend(item["aliases"])
            names.extend(item["identifiers"].values())
            if (
                query is not None
                and query.strip().casefold() not in " ".join(names).casefold()
            ):
                continue
            matches.append(item)
        return _success(MODEL_OPERATION, matches)

    async def list_hardware_test_records(self, hardware_id=None):
        if hardware_id is not None and not _identity(hardware_id):
            return hardware_error(
                TEST_OPERATION, "INVALID_ARGUMENT", "Invalid hardware_id."
            )
        try:
            records = load_test_records(self.records_path)
            validate_test_links(load_hardware(self.catalog_path), records)
        except (OSError, OverflowError, ValueError, TypeError, RecursionError) as error:
            return _read_error(TEST_OPERATION, error)
        return _success(
            TEST_OPERATION,
            [
                record
                for record in records
                if hardware_id is None or record["hardware_id"] == hardware_id
            ],
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write-docs", action="store_true")
    mode.add_argument("--check-docs", action="store_true")
    args = parser.parse_args()
    from tools.hardware_catalog import main as catalog_main

    return catalog_main(["--write-docs" if args.write_docs else "--check"])


if __name__ == "__main__":
    main()
