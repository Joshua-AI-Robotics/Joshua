import argparse
import copy
import datetime
import hashlib
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse

from jsonschema import Draft202012Validator, FormatChecker, ValidationError

ROOT = Path(__file__).parents[1]
CATALOG_ROOT = ROOT / "docs/hardware_catalog"
SOFTWARE_ROOT = ROOT / "docs/device_software_catalog"
MAX_RECORD_BYTES = 262144
MAX_RECORDS = 10000
MAX_CATALOG_BYTES = 33554432
MAX_RESPONSE_BYTES = 60000
QUERY_OPERATION = "query_hardware_catalog"
QUERY_SCOPE = "hardware_validation_catalog"
QUERY_LIMITATIONS = [
    "Historical results apply only to their recorded setup and "
    "software. Missing provenance remains unknown. Integration "
    "means an implemented software path, not physical "
    "qualification or permission to operate hardware. No device "
    "was opened."
]
QUERY_INPUT_SCHEMA = {
    "type": "object",
    "properties": {
        "view": {"enum": ["devices", "history"], "default": "devices"},
        "hardware_id": {"type": "string", "pattern": "^[a-z][a-z0-9_-]{0,127}$"},
        "kind": {"type": "string", "pattern": "^[a-z][a-z0-9_-]{0,127}$"},
        "query": {"type": "string", "minLength": 1, "maxLength": 256},
        "result": {"enum": ["passed", "failed", "inconclusive", "not_run"]},
        "validated_by": {"type": "string", "minLength": 1, "maxLength": 256},
        "limit": {"type": "integer", "minimum": 1, "maximum": 50, "default": 20},
        "cursor": {"type": "string", "maxLength": 80},
    },
    "required": [],
    "additionalProperties": False,
}
QUERY_REPORT_SCHEMA = {
    "type": "object",
    "properties": {
        "schema_version": {"const": 2},
        "scope": {"const": QUERY_SCOPE},
        "operation": {"const": QUERY_OPERATION},
        "status_code": {
            "enum": [
                "OK",
                "INVALID_ARGUMENT",
                "UNAVAILABLE",
                "DATA_LOSS",
                "RESOURCE_EXHAUSTED",
            ]
        },
        "message": {"type": "string"},
        "data": {"type": "object"},
        "limitations": {"const": QUERY_LIMITATIONS},
    },
    "required": [
        "schema_version",
        "scope",
        "operation",
        "status_code",
        "message",
        "data",
        "limitations",
    ],
    "additionalProperties": False,
}


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON field: {key}")
        result[key] = value
    return result


def read_json(path):
    with Path(path).open("rb") as stream:
        raw = stream.read(MAX_RECORD_BYTES + 1)
    if len(raw) > MAX_RECORD_BYTES:
        raise OverflowError(f"Record exceeds {MAX_RECORD_BYTES} bytes: {path}")
    value = json.loads(raw.decode("utf-8"), object_pairs_hook=unique_pairs)
    json.dumps(value, allow_nan=False)
    return value


def utc_time(value):
    if not isinstance(value, str) or not value.endswith("Z"):
        raise ValueError("Timestamps must be ISO 8601 UTC ending in Z")
    parsed = datetime.datetime.fromisoformat(value[:-1] + "+00:00")
    if "T" not in value or parsed.utcoffset() != datetime.timedelta(0):
        raise ValueError("Invalid UTC timestamp")
    return parsed


def local_reference(reference, root=ROOT):
    path, _, fragment = reference.partition("#")
    if (
        not path
        or "\\" in path
        or any(part in ("", ".", "..") for part in path.split("/"))
        or PurePosixPath(path).is_absolute()
    ):
        raise ValueError(f"Unsafe catalog reference: {reference}")
    candidate = root / path
    if not candidate.is_file():
        raise ValueError(f"Missing or escaping catalog reference: {reference}")
    if fragment and candidate.suffix == ".md":
        if not re.fullmatch(r"[a-z0-9_-]+", fragment):
            raise ValueError(f"Invalid anchor: {reference}")
    return candidate


def external_reference(value):
    parsed = urlparse(value)
    return (
        parsed.scheme == "https"
        and bool(parsed.netloc)
        and not parsed.username
        and not parsed.password
    )


def accepts(check, observed):
    expected, operator = check["expected"], check["operator"]

    def numeric(value):
        return type(value) in (int, float) and math.isfinite(value)

    if operator == "eq":
        return (
            type(observed) is type(expected)
            and observed == expected
            or numeric(observed)
            and numeric(expected)
            and observed == expected
        )
    if operator == "between":
        if (
            not isinstance(expected, list)
            or len(expected) != 2
            or not all(numeric(v) for v in expected)
            or expected[0] > expected[1]
        ):
            raise ValueError("between needs two ordered finite numeric bounds")
        return numeric(observed) and expected[0] <= observed <= expected[1]
    if not numeric(expected):
        raise ValueError("Numeric comparison needs a finite numeric threshold")
    return numeric(observed) and (
        observed >= expected if operator == "gte" else observed <= expected
    )


def validate_review(record):
    fields = ("validated_at", "validated_by", "review_reference")
    values = [record[key] for key in fields]
    if any(v is not None for v in values) and not all(v is not None for v in values):
        raise ValueError(
            "Validation needs reviewer, timestamp and review reference " "together"
        )
    if values[0] is not None:
        utc_time(values[0])
        if not external_reference(values[2]):
            raise ValueError("Review reference must be an HTTPS review URL")
    return all(v is not None for v in values)


def operator_identity(operator):
    provider, username = operator["provider"], operator["username"]
    return provider, username.lower() if provider == "github" else username


def validate_device(device, root=ROOT, check_files=True):
    if device.get("discovery") and not external_reference(
        device["discovery"]["source_url"]
    ):
        raise ValueError("Discovery needs an HTTPS source without credentials")
    integration = device["integration"]
    if integration["status"] == "integrated" and (
        not integration["driver"]
        or not integration["implementations"]
        or not integration["presets"]
    ):
        raise ValueError(
            "Integrated hardware needs a driver, implementation and preset"
        )
    if check_files:
        for path in (
            integration["implementations"]
            + integration["presets"]
            + [integration["guide"]]
        ):
            local_reference(path, root)
    tests = {test["id"]: test for test in device["validation_history"]}
    if len(tests) != len(device["validation_history"]):
        raise ValueError("Duplicate test ID in device")
    specifications = {}
    for test in tests.values():
        setup, spec = test["setup"], test["specification"]
        roles = [subject["role"] for subject in setup["subjects"]]
        if len(set(roles)) != len(roles):
            raise ValueError("Duplicate setup subject role")
        if device["id"] not in [s["hardware_id"] for s in setup["subjects"]]:
            raise ValueError("Test setup must include this device")
        if any(f["subject"] not in roles for f in setup["firmware"]):
            raise ValueError("Unknown firmware subject")
        if device["kind"] not in spec["kinds"]:
            raise ValueError("Specification does not apply to hardware kind")
        version = (spec["id"], spec["version"])
        if version in specifications and specifications[version] != spec:
            raise ValueError("Changed criteria need a new specification version")
        specifications[version] = spec
        if check_files:
            for config in setup["configuration"]:
                local_reference(config["path"], root)
        times = [test["started_at"], test["completed_at"]]
        if any(times) and (not all(times) or utc_time(times[0]) > utc_time(times[1])):
            raise ValueError("Test needs ordered UTC start and completion times")
        if test["date"]:
            datetime.date.fromisoformat(test["date"])
        reviewed = validate_review(test)
        if (test["review_status"] != "unreviewed") != reviewed:
            raise ValueError("Review status disagrees with attribution")
        if (
            reviewed
            and times[1]
            and utc_time(test["validated_at"]) < utc_time(times[1])
        ):
            raise ValueError("Validation predates completion")
        if test["provenance"] == "complete" and (
            not all(times)
            or not test["executed_by"]
            or not test["software"]["joshua_commit"]
        ):
            raise ValueError("Complete provenance needs operator, times and commit")
        for key in (
            "date",
            "date_timezone",
            "started_at",
            "completed_at",
            "executed_by",
            "validated_at",
            "validated_by",
            "organization",
            "review_reference",
        ):
            if test[key] is None and key not in test["unknowns"]:
                raise ValueError(f"Unknown {key} needs a reason")
        if (
            test["software"]["joshua_commit"] is None
            and "software.joshua_commit" not in test["unknowns"]
        ):
            raise ValueError("Unknown tested commit needs a reason")
        evidence = {item["id"]: item for item in test["evidence"]}
        if len(evidence) != len(test["evidence"]):
            raise ValueError("Duplicate evidence ID")
        for item in evidence.values():
            availability = item["availability"]
            if availability == "inline":
                if (
                    not item.get("notes")
                    or hashlib.sha256(item["notes"].encode()).hexdigest()
                    != item["sha256"]
                ):
                    raise ValueError("Inline evidence checksum mismatch")
            elif availability == "external":
                if not external_reference(item.get("path", "")) or not item["sha256"]:
                    raise ValueError("External evidence needs HTTPS URL and checksum")
            elif availability == "retained":
                if not item.get("path") or not item["sha256"]:
                    raise ValueError("Retained attachment needs path and checksum")
                if (
                    check_files
                    and hashlib.sha256(
                        local_reference(item["path"], root).read_bytes()
                    ).hexdigest()
                    != item["sha256"]
                ):
                    raise ValueError("Attachment checksum mismatch")
        expected = {check["id"]: check for check in spec["checks"]}
        if len(expected) != len(spec["checks"]):
            raise ValueError("Duplicate specification check")
        seen = set()
        outcomes = []
        for check in spec["checks"]:
            accepts(check, None)
        for outcome in test["checks"]:
            cid, status = outcome["check_id"], outcome["outcome"]
            if (
                cid not in expected
                or cid in seen
                or not set(outcome["evidence_ids"]) <= set(evidence)
            ):
                raise ValueError("Unknown/duplicate check or evidence")
            seen.add(cid)
            if status in ("passed", "failed"):
                if outcome["observed"] is None or (status == "passed") != accepts(
                    expected[cid], outcome["observed"]
                ):
                    raise ValueError("Outcome contradicts observed acceptance check")
            outcomes.append(status)
        if seen != set(expected):
            raise ValueError("Every check needs an explicit outcome")
        result = (
            "failed"
            if "failed" in outcomes
            else (
                "passed"
                if set(outcomes) == {"passed"}
                else ("not_run" if set(outcomes) == {"not_run"} else "inconclusive")
            )
        )
        if test["result"] != result:
            raise ValueError("Overall result contradicts checks")
        for relation in ("supersedes", "reproduces"):
            if test[relation] and (
                test[relation] not in tests or test[relation] == test["id"]
            ):
                raise ValueError("Unknown/self test history reference")
        if test["review_status"] == "independently_reproduced":
            original = tests.get(test["reproduces"])
            if (
                not original
                or not original["executed_by"]
                or not test["executed_by"]
                or operator_identity(original["executed_by"])
                == operator_identity(test["executed_by"])
            ):
                raise ValueError(
                    "Independent reproduction needs distinct recorded operators"
                )
    compatibility = device.get("compatibility")
    if compatibility:
        if (
            not integration["driver"]
            or not integration["implementations"]
            or not integration["presets"]
        ):
            raise ValueError("Compatibility projection needs driver, source and preset")
        for ident in compatibility["run_order"]:
            if (
                ident not in tests
                or not tests[ident]["date"]
                or not tests[ident]["software"]["joshua_commit"]
            ):
                raise ValueError(
                    "Compatibility history needs a known test, date and commit"
                )
    reviews = {review["id"]: review for review in device["support_history"]}
    if len(reviews) != len(device["support_history"]):
        raise ValueError("Duplicate support review ID")
    for review in reviews.values():
        if not set(review["run_ids"]) <= set(tests):
            raise ValueError("Support review references an unknown test")
        demonstrated = {
            check["capability"]
            for run_id in review["run_ids"]
            for check in tests[run_id]["specification"]["checks"]
            if any(
                r["check_id"] == check["id"] and r["outcome"] == "passed"
                for r in tests[run_id]["checks"]
            )
        }
        attribution = validate_review(review)
        if review["status"] != "historical_unreviewed" and not attribution:
            raise ValueError("Support claims need reviewer/time/reference")
        if (
            review["status"] in ("supported", "limited")
            and not set(review["capabilities"]) <= demonstrated
        ):
            raise ValueError("Support claim has no passing evidence in its cited runs")


def load_devices(directory=CATALOG_ROOT, root=ROOT, check_files=True):
    directory = Path(directory)
    schema = read_json(directory / "catalog.schema.json")
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema, format_checker=FormatChecker())
    devices = {}
    total = 0
    for path in sorted(directory.glob("*.json")):
        if path.name == "catalog.schema.json":
            continue
        total += path.stat().st_size
        if total > MAX_CATALOG_BYTES or len(devices) >= MAX_RECORDS:
            raise OverflowError("Catalog admission budget exceeded")
        device = read_json(path)
        if "REPLACE" in json.dumps(device):
            raise ValueError("Unfilled device template")
        try:
            validator.validate(device)
        except ValidationError as error:
            raise ValueError(f"{path}: {error.message}") from error
        if path.stem != device["id"] or device["id"] in devices:
            raise ValueError("Duplicate or mismatched device ID")
        validate_device(device, root, check_files)
        devices[device["id"]] = device
    for device in devices.values():
        for test in device["validation_history"]:
            if any(s["hardware_id"] not in devices for s in test["setup"]["subjects"]):
                raise ValueError("Setup references unknown catalog device")
    return devices


def load_software(directory=SOFTWARE_ROOT, root=ROOT, check_files=True):
    directory = Path(directory)
    schema = read_json(directory / "catalog.schema.json")
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema, format_checker=FormatChecker())
    records, implementations = {}, set()
    total = 0
    for path in sorted(directory.glob("*.json")):
        if path.name == "catalog.schema.json":
            continue
        total += path.stat().st_size
        if total > MAX_CATALOG_BYTES or len(records) >= MAX_RECORDS:
            raise OverflowError("Software catalog admission budget exceeded")
        record = read_json(path)
        try:
            validator.validate(record)
        except ValidationError as error:
            raise ValueError(f"{path}: {error.message}") from error
        if path.stem != record["id"] or record["id"] in records:
            raise ValueError("Duplicate or mismatched software ID")
        if not external_reference(record["source_url"]):
            raise ValueError("Software needs an HTTPS source without credentials")
        integration = record["integration"]
        if integration["status"] == "integrated":
            implementation = integration["implementation"]
            if not implementation or implementation in implementations:
                raise ValueError("Integrated software needs a unique implementation")
            if check_files:
                local_reference(implementation, root)
            implementations.add(implementation)
        records[record["id"]] = record
    return records


def export_catalog(root=ROOT):
    root = Path(root)
    revision = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    if subprocess.check_output(
        ["git", "-C", str(root), "status", "--porcelain", "--untracked-files=all"],
        text=True,
    ).strip():
        raise ValueError("Export requires a clean, committed Joshua checkout")
    paths = set(
        subprocess.check_output(
            ["git", "-C", str(root), "ls-tree", "-r", "--name-only", revision],
            text=True,
        ).splitlines()
    )
    devices = load_devices(root / "docs/hardware_catalog", root)
    software = load_software(root / "docs/device_software_catalog", root)
    categories = {
        "board": "Compute & boards",
        "host": "Compute & boards",
        "camera": "Cameras",
        "actuator": "Actuators",
        "sensor": "Sensors",
        "motor_driver": "Motor controllers",
        "assembly": "Robot assemblies",
        "transport_adapter": "Communication adapters",
        "power": "Power",
        "other": "Other hardware",
    }
    repository = "https://github.com/Joshua-AI-Robotics/Joshua/blob/" + revision + "/"
    result = dict(schema_version=1, joshua_revision=revision, hardware=[], software=[])
    dates = []
    for device in devices.values():
        path = "docs/hardware_catalog/" + device["id"] + ".json"
        references = [
            path,
            device["integration"]["guide"].partition("#")[0],
            *device["integration"]["implementations"],
            *device["integration"]["presets"],
        ]
        if any(reference not in paths for reference in references):
            raise ValueError("Hardware export references an uncommitted source")
        discovery = device.get("discovery", {})
        if discovery.get("snapshot_date"):
            dates.append(discovery["snapshot_date"])
        entry = dict(
            id=device["id"],
            name=device["model"],
            vendor=device["manufacturer"],
            category=discovery.get("category", categories[device["kind"]]),
            source_url=discovery.get("source_url", repository + path),
            aliases=device["aliases"],
            capabilities=device["capabilities"],
            integrated=device["integration"]["status"] == "integrated",
            status=device["integration"]["status"],
            counts_toward_coverage="coverage_exclusion" not in discovery,
            implementation_url=repository + path,
        )
        if "coverage_exclusion" in discovery:
            entry["coverage_exclusion"] = discovery["coverage_exclusion"]
        result["hardware"].append(entry)
    for record in software.values():
        path = "docs/device_software_catalog/" + record["id"] + ".json"
        implementation = record["integration"]["implementation"]
        integrated = record["integration"]["status"] == "integrated"
        if path not in paths or (integrated and implementation not in paths):
            raise ValueError("Software export references an uncommitted source")
        dates.append(record["snapshot_date"])
        result["software"].append(
            dict(
                id=record["id"],
                name=record["name"],
                vendor=record["vendor"],
                category=record["category"],
                source_url=record["source_url"],
                aliases=record["aliases"],
                description=record["description"],
                integrated=integrated,
                status=record["integration"]["status"],
                counts_toward_coverage=True,
                implementation_url=repository
                + (implementation if integrated else path),
            )
        )
    if not devices or not software or not dates:
        raise ValueError("Catalog export needs hardware, software and dated discovery")
    result["snapshot_date"] = max(dates)
    return result


def legacy_exports(devices):
    models = []
    runs = []
    for device in devices.values():
        compatibility = device.get("compatibility")
        if not compatibility:
            continue
        integration = device["integration"]
        models.append(
            dict(
                id=device["id"],
                kind=device["kind"],
                manufacturer=device["manufacturer"],
                model=device["model"],
                aliases=device["aliases"],
                identifiers=device["identifiers"],
                driver=integration["driver"],
                implementation=integration["implementations"][0],
                preset=integration["presets"][0],
                support_scope=integration["support_scope"],
            )
        )
        byid = {test["id"]: test for test in device["validation_history"]}
        for ident in compatibility["run_order"]:
            test = byid[ident]
            runs.append(
                dict(
                    id=test["id"],
                    hardware_id=device["id"],
                    date=test["date"],
                    joshua_commit=test["software"]["joshua_commit"],
                    environment=test["setup"]["environment"],
                    evidence=f"docs/hardware_catalog/{device['id']}.json",
                    results=test["measurements"],
                    limitations=test["limitations"],
                )
            )
    models.sort(key=lambda item: devices[item["id"]]["compatibility"]["model_order"])
    return dict(schema_version=1, hardware=models), dict(schema_version=1, records=runs)


def query_error(code, message):
    return dict(
        schema_version=2,
        scope=QUERY_SCOPE,
        operation=QUERY_OPERATION,
        status_code=code,
        message=message,
        data={},
        limitations=list(QUERY_LIMITATIONS),
    )


def device_summary(device):
    return {
        key: device[key]
        for key in (
            "schema_version",
            "id",
            "kind",
            "manufacturer",
            "model",
            "aliases",
            "capabilities",
            "integration",
        )
    } | {
        "source": f"docs/hardware_catalog/{device['id']}.json",
        "validation": {
            "tests": len(device["validation_history"]),
            "passed": sum(
                t["result"] == "passed" for t in device["validation_history"]
            ),
            "failed": sum(
                t["result"] == "failed" for t in device["validation_history"]
            ),
            "reviewed": sum(
                t["review_status"] != "unreviewed" for t in device["validation_history"]
            ),
        },
    }


def validate_query_report(report):
    try:
        Draft202012Validator(QUERY_REPORT_SCHEMA).validate(report)
    except ValidationError as error:
        raise ValueError(error.message) from error
    if report["status_code"] != "OK":
        if report["data"]:
            raise ValueError("Failed query must have no data")
        return
    data = report["data"]
    if (
        set(data) != {"view", "entries", "next_cursor", "source"}
        or data["view"] not in ("devices", "history")
        or data["source"] != "docs/hardware_catalog"
        or type(data["entries"]) is not list
        or len(data["entries"]) > 50
    ):
        raise ValueError("Invalid catalog query report")
    if data["next_cursor"] is not None and not re.fullmatch(
        "[0-9a-f]{64}:[0-9]+", data["next_cursor"]
    ):
        raise ValueError("Invalid cursor")
    schema = read_json(CATALOG_ROOT / "catalog.schema.json")
    for entry in data["entries"]:
        if data["view"] == "history":
            payload = {k: v for k, v in entry.items() if k != "hardware_id"}
            selected = {**schema, "$ref": "#/$defs/run"}
            selected.pop("required")
            selected.pop("properties")
            selected.pop("additionalProperties")
            selected.pop("allOf", None)
            try:
                Draft202012Validator(selected, format_checker=FormatChecker()).validate(
                    payload
                )
            except ValidationError as error:
                raise ValueError(error.message) from error
        elif not isinstance(entry.get("id"), str) or not isinstance(
            entry.get("validation"), dict
        ):
            raise ValueError("Invalid device summary")


def query_catalog(directory=CATALOG_ROOT, **filters):
    try:
        Draft202012Validator(QUERY_INPUT_SCHEMA).validate(filters)
        if any(
            isinstance(v, str) and (not v.strip() or any(ord(c) < 32 for c in v))
            for v in filters.values()
        ):
            raise ValueError("Invalid filter text")
        view = filters.get("view", "devices")
        limit = filters.get("limit", 20)
        if view != "history" and ("result" in filters or "validated_by" in filters):
            raise ValueError("Result/reviewer filters apply to history")
    except (ValidationError, ValueError, TypeError) as error:
        return query_error("INVALID_ARGUMENT", str(error)[:512])
    try:
        devices = load_devices(directory, check_files=False)
        selected = [
            d
            for d in devices.values()
            if ("hardware_id" not in filters or d["id"] == filters["hardware_id"])
            and ("kind" not in filters or d["kind"] == filters["kind"])
        ]
        entries = (
            [device_summary(d) for d in selected]
            if view == "devices"
            else [
                dict(hardware_id=d["id"], **t)
                for d in selected
                for t in d["validation_history"]
            ]
        )
        if "result" in filters:
            entries = [e for e in entries if e["result"] == filters["result"]]
        if "validated_by" in filters:
            entries = [
                e
                for e in entries
                if e["validated_by"]
                and e["validated_by"]["username"].casefold()
                == filters["validated_by"].casefold()
            ]
        if "query" in filters:
            entries = [
                e
                for e in entries
                if filters["query"].strip().casefold()
                in json.dumps(e, ensure_ascii=False).casefold()
            ]
        signature = hashlib.sha256(
            json.dumps(
                [
                    view,
                    {k: v for k, v in filters.items() if k not in ("cursor", "limit")},
                    entries,
                ],
                sort_keys=True,
                allow_nan=False,
            ).encode()
        ).hexdigest()
        offset = 0
        if "cursor" in filters:
            match = re.fullmatch("([0-9a-f]{64}):([0-9]+)", filters["cursor"])
            if not match or match[1] != signature or int(match[2]) > len(entries):
                return query_error(
                    "INVALID_ARGUMENT", "Invalid, stale or mismatched cursor"
                )
            offset = int(match[2])
        report = query_error(
            "OK",
            "Recorded device integration and historical evidence; no live "
            "assessment.",
        )
        report["data"] = dict(
            view=view, entries=[], next_cursor=None, source="docs/hardware_catalog"
        )
        for entry in entries[offset : offset + limit]:
            candidate = copy.deepcopy(report)
            candidate["data"]["entries"].append(entry)
            used = offset + len(candidate["data"]["entries"])
            candidate["data"]["next_cursor"] = (
                f"{signature}:{used}" if used < len(entries) else None
            )
            if (
                len(json.dumps(candidate, ensure_ascii=True).encode())
                > MAX_RESPONSE_BYTES
            ):
                if not report["data"]["entries"]:
                    return query_error(
                        "RESOURCE_EXHAUSTED",
                        "One history entry exceeds the response budget; read the "
                        "device source file",
                    )
                break
            report = candidate
        return report
    except OSError:
        return query_error("UNAVAILABLE", "Device catalog unavailable")
    except OverflowError:
        return query_error("RESOURCE_EXHAUSTED", "Catalog admission limit exceeded")
    except (ValueError, TypeError, KeyError, RecursionError):
        return query_error("DATA_LOSS", "Invalid device catalog data")


def check_history(base_commit, root=ROOT):
    if not re.fullmatch("[0-9a-f]{40}", base_commit):
        raise ValueError("History check needs the full base commit SHA")
    command = ["git", "-c", f"safe.directory={root.resolve()}", "-C", str(root)]
    names = subprocess.run(
        command
        + ["ls-tree", "-r", "--name-only", base_commit, "--", "docs/hardware_catalog/"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.splitlines()
    for name in names:
        relative = name.removeprefix("docs/hardware_catalog/")
        if (
            "/" in relative
            or not name.endswith(".json")
            or relative == "catalog.schema.json"
        ):
            continue
        previous = json.loads(
            subprocess.run(
                command + ["show", f"{base_commit}:{name}"],
                capture_output=True,
                check=True,
            ).stdout
        )
        if previous.get("schema_version") != 2:
            continue
        current = root / name
        if not current.is_file():
            raise ValueError("Historical device removed")
        updated = read_json(current)
        if any(
            previous[k] != updated[k] for k in ("id", "kind", "manufacturer", "model")
        ):
            raise ValueError("Device identity changed; use a new device ID")
        for section in ("validation_history", "support_history"):
            entries = {entry["id"]: entry for entry in updated[section]}
            if any(entries.get(old["id"]) != old for old in previous[section]):
                raise ValueError(
                    "Historical test/review changed or removed; append a correction"
                )


def scaffold(ident, kind, schema):
    if not re.fullmatch("[a-z][a-z0-9_-]{0,127}", ident):
        raise ValueError("Invalid device ID")
    category = next(
        c["then"]["properties"]["category_details"]["properties"]
        for c in schema["allOf"]
        if c["if"]["properties"]["kind"]["const"] == kind
    )
    return dict(
        schema_version=2,
        id=ident,
        kind=kind,
        manufacturer="REPLACE manufacturer",
        model="REPLACE exact model",
        aliases=[],
        revisions=[],
        identifiers={},
        capabilities=["replace-capability"],
        specifications=[],
        integration=dict(
            status="candidate",
            driver=None,
            implementations=[],
            presets=[],
            guide="AGENTS.md",
            support_scope="REPLACE intended scope and limitations",
        ),
        documentation=[],
        maintainers=[],
        category_details={
            k: [] if v["type"] == "array" else None for k, v in category.items()
        },
        limitations=["REPLACE untested features and identity gaps"],
        validation_history=[],
        support_history=[],
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="One source file per hardware device; no hardware access"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--query", choices=("devices", "history"))
    mode.add_argument("--check-history")
    mode.add_argument("--new", action="store_true")
    mode.add_argument("--export", action="store_true")
    parser.add_argument(
        "--kind",
        choices=(
            "board",
            "camera",
            "actuator",
            "motor_driver",
            "sensor",
            "transport_adapter",
            "host",
            "power",
            "assembly",
            "other",
        ),
    )
    parser.add_argument("--id")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--hardware-id")
    parser.add_argument("--text")
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--cursor")
    args = parser.parse_args(argv)
    workspace = Path(os.environ.get("BUILD_WORKSPACE_DIRECTORY", ROOT))
    directory = workspace / "docs/hardware_catalog"
    try:
        if args.export:
            report = json.dumps(export_catalog(workspace), ensure_ascii=False, indent=2)
            if args.output:
                args.output.write_text(report + "\n", encoding="utf-8")
            else:
                print(report)
            return 0
        if args.new:
            if not args.id or not args.output:
                parser.error("--new needs --id and --output")
            device = scaffold(
                args.id,
                args.kind or "board",
                read_json(directory / "catalog.schema.json"),
            )
            with args.output.open("x", encoding="utf-8") as stream:
                stream.write(json.dumps(device, indent=2) + "\n")
            print(
                "Draft created. Complete its integration and validation "
                "details before submission."
            )
            return 0
        if args.check_history:
            check_history(args.check_history, workspace)
        elif args.query:
            filters = dict(view=args.query, limit=args.limit)
            filters.update(
                {
                    k: v
                    for k, v in [
                        ("hardware_id", args.hardware_id),
                        ("kind", args.kind),
                        ("query", args.text),
                        ("cursor", args.cursor),
                    ]
                    if v is not None
                }
            )
            report = query_catalog(directory, **filters)
            print(json.dumps(report, indent=2))
            return int(report["status_code"] != "OK")
        else:
            load_devices(directory, workspace)
            load_software(workspace / "docs/device_software_catalog", workspace)
        print("Device catalog checks passed.")
        return 0
    except (
        ValueError,
        OSError,
        TypeError,
        OverflowError,
        subprocess.CalledProcessError,
    ) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    sys.exit(main())
