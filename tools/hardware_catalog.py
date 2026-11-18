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
COLLECTIONS = {
    "models": "model",
    "setups": "setup",
    "specifications": "specification",
    "runs": "run",
    "assessments": "assessment",
}
MAX_RECORD_BYTES = 262144
MAX_RECORDS = 10000
MAX_CATALOG_BYTES = 33554432
MAX_RESPONSE_BYTES = 60000
QUERY_OPERATION = "query_hardware_catalog"
QUERY_SCOPE = "hardware_validation_catalog"
QUERY_LIMITATIONS = [
    "Historical evidence applies only to its recorded setup, "
    "specification and software. Unknown provenance is explicit. "
    "Catalog results neither assess connected hardware nor "
    "authorize device access. No driver, ROS graph or device was "
    "opened."
]
QUERY_INPUT_SCHEMA = {
    "type": "object",
    "properties": {
        "entity": {"type": "string", "enum": list(COLLECTIONS), "default": "models"},
        "hardware_id": {"type": "string", "pattern": "^[a-z][a-z0-9_-]{0,127}$"},
        "query": {"type": "string", "minLength": 1, "maxLength": 256},
        "result": {
            "type": "string",
            "enum": ["passed", "failed", "inconclusive", "not_run"],
        },
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
        "schema_version": {"const": 1},
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


def validator_for(schema, kind):
    selected = {key: value for key, value in schema.items() if key != "oneOf"}
    selected["$ref"] = f"#/$defs/{kind}"
    return Draft202012Validator(selected, format_checker=FormatChecker())


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


def validate_catalog(catalog, root=ROOT, check_files=True):
    models, setups, specifications, runs, assessments = (
        catalog[key] for key in COLLECTIONS
    )
    for collection, kind in ((models, "model"), (runs, "run")):
        orders = [
            entry["legacy_order"]
            for entry in collection.values()
            if (
                entry["integration"]["legacy_export"]
                if kind == "model"
                else entry["legacy_export"]
            )
        ]
        if any(type(order) is not int or order < 0 for order in orders) or len(
            set(orders)
        ) != len(orders):
            raise ValueError("Legacy exports need distinct explicit ordering")
    for model in models.values():
        integration = model["integration"]
        if integration["legacy_export"] and (
            integration["status"] != "intended"
            or not integration["driver"]
            or not integration["implementations"]
            or not integration["presets"]
        ):
            raise ValueError(
                "Legacy supported models need an intended implementation and " "preset"
            )
        if check_files:
            for path in (
                integration["implementations"]
                + integration["presets"]
                + [integration["guide"]]
            ):
                local_reference(path, root)
            for source in model["documentation"]:
                if not external_reference(source):
                    local_reference(source, root)
            for spec in model["specifications"]:
                if not external_reference(spec["source"]):
                    local_reference(spec["source"], root)
    for setup in setups.values():
        roles = set()
        for subject in setup["subjects"]:
            if subject["hardware_id"] not in models or subject["role"] in roles:
                raise ValueError("Unknown hardware or duplicate setup role")
            roles.add(subject["role"])
        for firmware in setup["firmware"]:
            if firmware["subject"] not in roles:
                raise ValueError("Firmware references an unknown setup subject")
        if check_files:
            for config in setup["configuration"]:
                local_reference(config["path"], root)
    for spec in specifications.values():
        ids = [check["id"] for check in spec["checks"]]
        if len(ids) != len(set(ids)):
            raise ValueError("Duplicate specification check")
        for check in spec["checks"]:
            accepts(check, None)
        if check_files and spec["source"]:
            local_reference(spec["source"], root)
    for run in runs.values():
        if (
            run["hardware_id"] not in models
            or run["setup_id"] not in setups
            or run["specification_id"] not in specifications
        ):
            raise ValueError("Run references unknown hardware, setup or specification")
        setup, spec = setups[run["setup_id"]], specifications[run["specification_id"]]
        if (
            run["hardware_id"] not in {s["hardware_id"] for s in setup["subjects"]}
            or run["specification_version"] != spec["version"]
        ):
            raise ValueError(
                "Run does not match setup hardware or specification version"
            )
        if models[run["hardware_id"]]["kind"] not in spec["kinds"]:
            raise ValueError("Specification does not apply to this hardware kind")
        times = [run[key] for key in ("started_at", "completed_at")]
        if any(times) and (not all(times) or utc_time(times[0]) > utc_time(times[1])):
            raise ValueError("Run needs ordered start and completion timestamps")
        if run["date"]:
            datetime.date.fromisoformat(run["date"])
        reviewed = validate_review(run)
        if (run["review_status"] != "unreviewed") != reviewed:
            raise ValueError("Review status must agree with validation provenance")
        if (
            reviewed
            and run["completed_at"]
            and utc_time(run["validated_at"]) < utc_time(run["completed_at"])
        ):
            raise ValueError("Validation cannot predate test completion")
        if run["provenance"] == "complete" and (
            not all(times)
            or not run["executed_by"]
            or not run["software"]["joshua_commit"]
            or not run["date"]
            or not run["date_timezone"]
        ):
            raise ValueError(
                "Complete provenance needs times, operator, date/timezone and " "commit"
            )
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
            if run[key] is None and key not in run["unknowns"]:
                raise ValueError(f"Unknown {key} requires an explicit reason")
        if (
            run["software"]["joshua_commit"] is None
            and "software.joshua_commit" not in run["unknowns"]
        ):
            raise ValueError("Unknown tested commit needs an explicit reason")
        evidence_ids = {item["id"] for item in run["evidence"]}
        if len(evidence_ids) != len(run["evidence"]):
            raise ValueError("Duplicate evidence ID")
        for item in run["evidence"]:
            if item["availability"] == "retained":
                if not item["sha256"]:
                    raise ValueError("Retained evidence needs a checksum")
                if check_files:
                    file = local_reference(item["path"], root)
                    if hashlib.sha256(file.read_bytes()).hexdigest() != item["sha256"]:
                        raise ValueError(f"Evidence checksum mismatch: {file}")
            elif item["availability"] == "external" and (
                not external_reference(item["path"]) or not item["sha256"]
            ):
                raise ValueError("External evidence needs an HTTPS URL and checksum")
        checks = {check["id"]: check for check in spec["checks"]}
        seen, outcomes = set(), []
        for result in run["checks"]:
            cid, outcome = result["check_id"], result["outcome"]
            if (
                cid not in checks
                or cid in seen
                or not set(result["evidence_ids"]) <= evidence_ids
            ):
                raise ValueError("Unknown/duplicate check or evidence reference")
            seen.add(cid)
            if result["observed"] is None and outcome in ("passed", "failed"):
                raise ValueError("A pass/fail needs an observed value")
            accepted = accepts(checks[cid], result["observed"])
            if outcome in ("passed", "failed") and (outcome == "passed") != accepted:
                raise ValueError(
                    "Recorded outcome contradicts the acceptance criterion"
                )
            outcomes.append(outcome)
        if seen != set(checks):
            raise ValueError("Every specification check needs an explicit outcome")
        expected = (
            "failed"
            if "failed" in outcomes
            else (
                "passed"
                if set(outcomes) == {"passed"}
                else ("not_run" if set(outcomes) == {"not_run"} else "inconclusive")
            )
        )
        if run["result"] != expected:
            raise ValueError("Overall run result contradicts check outcomes")
        if run["reproduces"]:
            original = runs.get(run["reproduces"])
            if (
                original is None
                or original["id"] == run["id"]
                or any(
                    original[key] != run[key]
                    for key in (
                        "hardware_id",
                        "specification_id",
                        "specification_version",
                    )
                )
            ):
                raise ValueError(
                    "Reproduction needs another run with the same model and "
                    "specification"
                )
        if run["review_status"] == "independently_reproduced":
            original = runs.get(run["reproduces"])
            if (
                not original
                or not run["executed_by"]
                or not original["executed_by"]
                or original["executed_by"] == run["executed_by"]
            ):
                raise ValueError(
                    "Independent reproduction needs a linked run and distinct "
                    "recorded operators"
                )
        if run["legacy_export"] and (
            not run["date"] or not run["software"]["joshua_commit"]
        ):
            raise ValueError("Legacy run projection needs a recorded date and commit")
        if run["supersedes"] and (
            run["supersedes"] not in runs or run["supersedes"] == run["id"]
        ):
            raise ValueError("Unknown or self-referencing superseded run")
    for assessment in assessments.values():
        if assessment["hardware_id"] not in models or any(
            r not in runs for r in assessment["run_ids"]
        ):
            raise ValueError("Assessment references unknown hardware or run")
        if any(
            runs[r]["hardware_id"] != assessment["hardware_id"]
            for r in assessment["run_ids"]
        ):
            raise ValueError("Assessment references another hardware model")
        reviewed = validate_review(assessment)
        if assessment["status"] != "historical_unreviewed" and not reviewed:
            raise ValueError("Support assessment requires a recorded reviewer and time")
        if assessment["status"] in ("supported", "limited"):
            demonstrated = set()
            for run_id in assessment["run_ids"]:
                run = runs[run_id]
                spec = specifications[run["specification_id"]]
                checks = {c["id"]: c for c in spec["checks"]}
                demonstrated.update(
                    checks[c["check_id"]]["capability"]
                    for c in run["checks"]
                    if c["outcome"] == "passed"
                )
            if not set(assessment["capabilities"]) <= demonstrated:
                raise ValueError(
                    "Support assessment claims capabilities without passing evidence"
                )
        if assessment["supersedes"] and (
            assessment["supersedes"] not in assessments
            or assessment["supersedes"] == assessment["id"]
        ):
            raise ValueError("Unknown or self-referencing superseded assessment")


def load_catalog(directory=CATALOG_ROOT, root=ROOT, check_files=True):
    directory = Path(directory)
    schema = read_json(directory / "schemas/catalog.schema.json")
    Draft202012Validator.check_schema(schema)
    catalog, total, total_bytes = {}, 0, 0
    for collection, kind in COLLECTIONS.items():
        validator = validator_for(schema, kind)
        catalog[collection] = {}
        for file in sorted((directory / collection).rglob("*.json")):
            total += 1
            total_bytes += file.stat().st_size
            if total_bytes > MAX_CATALOG_BYTES:
                raise OverflowError("Catalog exceeds total byte admission limit")
            if total > MAX_RECORDS:
                raise OverflowError("Catalog exceeds record admission limit")
            record = read_json(file)
            if "REPLACE" in json.dumps(record):
                raise ValueError(f"Unfilled draft template: {file}")
            try:
                validator.validate(record)
            except ValidationError as error:
                raise ValueError(f"{file}: {error.message}") from error
            if file.stem != record["id"] or record["id"] in catalog[collection]:
                raise ValueError(f"Duplicate or mismatched record ID: {file}")
            catalog[collection][record["id"]] = record
    validate_catalog(catalog, root, check_files)
    return catalog


def legacy_exports(catalog):
    hardware, records = [], []
    for model in catalog["models"].values():
        integration = model["integration"]
        if integration["legacy_export"]:
            hardware.append(
                dict(
                    id=model["id"],
                    kind=model["kind"],
                    manufacturer=model["manufacturer"],
                    model=model["model"],
                    aliases=model["aliases"],
                    identifiers=model["identifiers"],
                    driver=integration["driver"],
                    implementation=integration["implementations"][0],
                    preset=integration["presets"][0],
                    support_scope=integration["support_scope"],
                )
            )
    for run in catalog["runs"].values():
        if run["legacy_export"]:
            records.append(
                dict(
                    id=run["id"],
                    hardware_id=run["hardware_id"],
                    date=run["date"],
                    joshua_commit=run["software"]["joshua_commit"],
                    environment=catalog["setups"][run["setup_id"]]["environment"],
                    evidence=run["evidence"][0]["path"],
                    results=run["measurements"],
                    limitations=run["limitations"],
                )
            )
    hardware.sort(key=lambda item: catalog["models"][item["id"]]["legacy_order"])
    records.sort(key=lambda item: catalog["runs"][item["id"]]["legacy_order"])
    return dict(schema_version=1, hardware=hardware), dict(
        schema_version=1, records=records
    )


def query_error(code, message):
    return dict(
        schema_version=1,
        scope=QUERY_SCOPE,
        operation=QUERY_OPERATION,
        status_code=code,
        message=message,
        data={},
        limitations=list(QUERY_LIMITATIONS),
    )


def validate_query_report(report):
    try:
        Draft202012Validator(QUERY_REPORT_SCHEMA).validate(report)
    except ValidationError as error:
        raise ValueError(error.message) from error
    if report["status_code"] != "OK":
        if report["data"]:
            raise ValueError("Failed query must have empty data")
        return
    data = report["data"]
    if (
        set(data) != {"entity", "entries", "next_cursor", "source"}
        or data["entity"] not in COLLECTIONS
        or data["source"] != "docs/hardware_catalog"
        or not isinstance(data["entries"], list)
        or len(data["entries"]) > 50
    ):
        raise ValueError("Invalid query result")
    if data["next_cursor"] is not None and not re.fullmatch(
        r"[0-9a-f]{64}:[0-9]+", data["next_cursor"]
    ):
        raise ValueError("Invalid next cursor")
    validator = validator_for(
        read_json(CATALOG_ROOT / "schemas/catalog.schema.json"),
        COLLECTIONS[data["entity"]],
    )
    for entry in data["entries"]:
        try:
            validator.validate(entry)
        except ValidationError as error:
            raise ValueError(error.message) from error


def query_catalog(directory=CATALOG_ROOT, **filters):
    try:
        Draft202012Validator(QUERY_INPUT_SCHEMA).validate(filters)
        if any(
            isinstance(v, str) and (not v.strip() or any(ord(c) < 32 for c in v))
            for v in filters.values()
        ):
            raise ValueError("Invalid text filter")
        entity, limit = filters.get("entity", "models"), filters.get("limit", 20)
        if ("result" in filters and entity != "runs") or (
            "validated_by" in filters and entity not in ("runs", "assessments")
        ):
            raise ValueError("Filter does not apply to entity")
    except (ValidationError, ValueError, TypeError, RecursionError) as error:
        return query_error("INVALID_ARGUMENT", str(error)[:512])
    try:
        catalog = load_catalog(directory, check_files=False)
        entries = list(catalog[entity].values())
        identity = filters.get("hardware_id")
        if identity:

            def belongs(entry):
                if entity == "models":
                    return entry["id"] == identity
                if entity == "setups":
                    return any(s["hardware_id"] == identity for s in entry["subjects"])
                if entity == "specifications":
                    return any(
                        r["hardware_id"] == identity
                        and r["specification_id"] == entry["id"]
                        for r in catalog["runs"].values()
                    )
                return entry["hardware_id"] == identity

            entries = [e for e in entries if belongs(e)]
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
                    entity,
                    {k: v for k, v in filters.items() if k not in ("cursor", "limit")},
                    entries,
                ],
                sort_keys=True,
                allow_nan=False,
            ).encode()
        ).hexdigest()
        offset = 0
        if "cursor" in filters:
            match = re.fullmatch(r"([0-9a-f]{64}):([0-9]+)", filters["cursor"])
            if not match or match[1] != signature or int(match[2]) > len(entries):
                return query_error(
                    "INVALID_ARGUMENT",
                    "Cursor is invalid, stale or belongs to another query",
                )
            offset = int(match[2])
        report = query_error(
            "OK",
            "Historical catalog records; connected hardware has not been " "assessed.",
        )
        report["data"] = dict(
            entity=entity, entries=[], next_cursor=None, source="docs/hardware_catalog"
        )
        for entry in entries[offset : offset + limit]:
            candidate = copy.deepcopy(report)
            candidate["data"]["entries"].append(entry)
            consumed = offset + len(candidate["data"]["entries"])
            candidate["data"]["next_cursor"] = (
                f"{signature}:{consumed}" if consumed < len(entries) else None
            )
            if (
                len(json.dumps(candidate, ensure_ascii=True).encode())
                > MAX_RESPONSE_BYTES
            ):
                if not report["data"]["entries"]:
                    return query_error(
                        "RESOURCE_EXHAUSTED",
                        "One record exceeds the response budget; use the source record "
                        "directly",
                    )
                break
            report = candidate
        return report
    except OSError:
        return query_error("UNAVAILABLE", "Catalog files are unavailable")
    except OverflowError:
        return query_error("RESOURCE_EXHAUSTED", "Catalog exceeds the admission budget")
    except (ValidationError, ValueError, TypeError, RecursionError):
        return query_error("DATA_LOSS", "Catalog records or references are invalid")


def render_catalog_markdown(hardware):
    lines = [
        "# Supported hardware",
        "",
        "<!-- Generated from hardware_catalog.json; do not edit by " "hand. -->",
        "",
        "This is the curated model list: intended support through " "existing drivers",
        "and example presets. Support scope is reviewed independently "
        "of test results.",
        "Adding a successful test does not automatically add a model "
        "or broaden its scope.",
        "An absent model is not necessarily incompatible with an " "existing driver.",
        "",
        "See [implemented components](SUPPORTED_COMPONENTS.md) "
        "for factory implementations",
        "and [test records](TESTED_HARDWARE.md) "
        "for dated measurements and environments.",
        "Protobuf presets remain authoritative for runtime settings. "
        "Resolve local device",
        "indices before running and follow the [hardware " "rules](../AGENTS.md).",
        "",
        'MCP: `list_supported_hardware(kind="camera", ' 'query="XWF-1080P")`.',
        "",
    ]
    lines[3:3] = [
        "For boards, devices, validation identities/times, failed runs "
        "and contribution templates, see the [full hardware "
        "catalog](hardware_catalog/INDEX.md).",
        "This page retains the original v1 camera compatibility view.",
        "",
    ]
    for item in hardware:
        lines.extend(
            [
                f'<a id="{item["id"]}"></a>',
                "",
                f'## {item["model"]}',
                "",
                f'- Hardware ID: `{item["id"]}` ({item["kind"]}).',
                f'- Manufacturer: {item["manufacturer"].rstrip(".")}.',
                "- Aliases: " + (", ".join(item["aliases"]) or "none recorded") + ".",
                f'- Driver: [{item["driver"]}](../{item["implementation"]}).',
                f'- Example: [{Path(item["preset"]).name}](../{item["preset"]}).',
                f'- Support scope: {item["support_scope"]}',
                "- Evidence: [separate test records]"
                f'(TESTED_HARDWARE.md#{item["id"]}).',
                "",
            ]
        )
    lines.extend(
        [
            "## Maintaining the list",
            "",
            "Edit [canonical model records](hardware_catalog/models/) "
            "for stable model information; the v1 JSON files are generated "
            "compatibility exports.",
            "A model can have zero or multiple tests. Add dated runs " "separately to",
            "[canonical run records](hardware_catalog/runs/), "
            "referencing the model's",
            "stable `hardware_id`. Do not copy model metadata "
            "into individual test records.",
            "",
            "In a Joshua Docker development shell, regenerate both guides " "with:",
            "",
            "```sh",
            "bazel run //tools:hardware_catalog_cli -- --write-docs",
            "```",
            "",
            "See [catalog contribution "
            "guidance](hardware_catalog/AGENTS.md) for templates, "
            "provenance and review. The catalog test checks schemas, "
            "duplicate IDs, "
            "unknown hardware references,",
            "source/preset/evidence links, and generated-guide consistency. "
            "Declare new",
            "referenced files in `docs/hardware_catalog/BUILD` "
            "for isolated Bazel checks.",
            "",
        ]
    )
    return "\n".join(lines)


def render_test_markdown(hardware, records):
    lines = [
        "# Hardware test records",
        "",
        "<!-- Generated from hardware_test_records.json; do not edit " "by hand. -->",
        "",
        "These are historical tests of specific units and software " "setups. The",
        "[supported-hardware list](SUPPORTED_HARDWARE.md) "
        "defines intended model support",
        "separately. Measurements below do not establish current "
        "readiness, "
        "every hardware",
        "revision's compatibility, or permission to run a device.",
        "",
        'MCP: `list_hardware_test_records(hardware_id="sunplus-spca2281-xwf-1080p")`.',
        "",
    ]
    lines[3:3] = [
        "For boards, devices, validation identities/times, failed runs "
        "and contribution templates, see the [full hardware "
        "catalog](hardware_catalog/INDEX.md).",
        "This page retains the original v1 camera compatibility view.",
        "",
    ]
    for item in hardware:
        lines.extend(
            [
                f'<a id="{item["id"]}"></a>',
                "",
                f'## {item["model"]}',
                "",
                "[Model details and support scope]"
                f'(SUPPORTED_HARDWARE.md#{item["id"]}).',
                "",
            ]
        )
        runs = [r for r in records if r["hardware_id"] == item["id"]]
        if not runs:
            lines.extend(["No test records recorded.", ""])
        for run in runs:
            environment = ", ".join(k + "=" + v for k, v in run["environment"].items())
            lines.extend(
                [
                    f'### {run["id"]}',
                    "",
                    f'- Hardware ID: `{run["hardware_id"]}`.',
                    f'- Tested: {run["date"]} at Joshua `{run["joshua_commit"]}`.',
                    f"- Environment: {environment}.",
                    f'- Evidence: [manual test record](../{run["evidence"]}).',
                    "",
                    "Recorded results:",
                    "",
                    "```json",
                    json.dumps(run["results"], indent=2, ensure_ascii=False),
                    "```",
                    "",
                    "Limitations:",
                    "",
                    *("- " + text for text in run["limitations"]),
                    "",
                ]
            )
    return "\n".join(lines)


def person_text(person):
    return (
        f"{person['provider']}:{person['username']}"
        if person
        else "Unknown / not recorded"
    )


def markdown_cell(value):
    return str(value).replace("|", "\\|").replace("\n", " ")


def render_model(model, catalog):
    lines = [
        f"# {model['manufacturer']} {model['model']}",
        "",
        "Generated from canonical catalog records. Historical results "
        "apply only to the listed setup and specification.",
        "",
        f"- Hardware ID: `{model['id']}`",
        f"- Kind: {model['kind']}",
        f"- Integration: {model['integration']['status']}",
        f"- Scope: {model['integration']['support_scope']}",
        f"- Capabilities: {', '.join(model['capabilities'])}",
        f"- Revisions: {', '.join(model['revisions']) or 'Unknown / not recorded'}",
        "",
        "## Model and setup references",
        "",
        f"- [Structured model](../models/{model['id']}.json)",
        f"- [Operating guide](../../../{model['integration']['guide']})",
        "",
        "## Validation history",
        "",
        "| Run / specification | Result | Date / actual time | "
        "Executed by | Validated by / time |",
        "| --- | --- | --- | --- | --- |",
    ]
    runs = [r for r in catalog["runs"].values() if r["hardware_id"] == model["id"]]
    for run in runs:
        lines.append(
            "| "
            + " | ".join(
                markdown_cell(v)
                for v in (
                    f"[{run['id']}](../runs/{run['id']}.json) / "
                    f"{run['specification_id']} v{run['specification_version']}",
                    run["result"],
                    f"{run['date'] or 'Unknown date'} / "
                    f"{run['started_at'] or 'exact time unknown'}",
                    person_text(run["executed_by"]),
                    f"{person_text(run['validated_by'])} / "
                    f"{run['validated_at'] or 'not recorded'}",
                )
            )
            + " |"
        )
    if not runs:
        lines.extend(
            [
                "",
                "No physical test run is recorded. Software support does not "
                "establish hardware validation.",
            ]
        )
    for run in runs:
        lines.extend(
            [
                "",
                f"## {run['id']}",
                "",
                f"- [Setup](../setups/{run['setup_id']}.json)",
                f"- [Specification](../specifications/{run['specification_id']}.json)",
                f"- Review status: {run['review_status']}",
                f"- Tested commit: "
                f"`{run['software']['joshua_commit'] or 'unknown'}`",
                f"- Started: {run['started_at'] or 'Unknown'}; completed: "
                f"{run['completed_at'] or 'Unknown'}",
                f"- Execution timezone/date context: "
                f"{run['date_timezone'] or 'Unknown'}",
                f"- Organization: {run['organization'] or 'Unknown'}",
                f"- Review: {run['review_reference'] or 'Not recorded'}",
                "",
                "| Check | Observed | Outcome |",
                "| --- | --- | --- |",
            ]
        )
        lines.extend(
            "| "
            + " | ".join(
                markdown_cell(v)
                for v in (c["check_id"], json.dumps(c["observed"]), c["outcome"])
            )
            + " |"
            for c in run["checks"]
        )
        lines.extend(["", "Evidence:", ""])
        for evidence in run["evidence"]:
            target = (
                "../../../" + evidence["path"]
                if evidence["availability"] == "retained"
                else evidence["path"]
            )
            label = (
                f"[{evidence['description']}]({target})"
                if evidence["availability"] != "not_retained"
                else evidence["description"]
            )
            lines.append(
                f"- {label}; {evidence['availability']}; SHA256 "
                f"`{evidence['sha256'] or 'not recorded'}`"
            )
        lines.extend(
            [
                "",
                "Limitations:",
                "",
                *("- " + item for item in run["limitations"]),
                "",
                "Provenance gaps:",
                "",
                *("- " + key + ": " + value for key, value in run["unknowns"].items()),
            ]
        )
    lines.extend(["", "## Support assessments", ""])
    assessments = [
        a for a in catalog["assessments"].values() if a["hardware_id"] == model["id"]
    ]
    for a in assessments:
        lines.extend(
            [
                f"- [{a['id']}](../assessments/{a['id']}.json): "
                f"**{a['status']}**. {a['scope']} Reviewer: "
                f"{person_text(a['validated_by'])}; time: "
                f"{a['validated_at'] or 'not recorded'}."
            ]
        )
    if not assessments:
        lines.append("No reviewed support assessment recorded.")
    lines.extend(
        [
            "",
            "## Model limitations",
            "",
            *("- " + item for item in model["limitations"]),
            "",
        ]
    )
    return "\n".join(lines)


def generated_files(catalog, directory=CATALOG_ROOT, root=ROOT):
    directory = Path(directory)
    hardware, records = legacy_exports(catalog)
    files = {
        root / "docs/hardware_catalog.json": json.dumps(hardware, indent=2) + "\n",
        root / "docs/hardware_test_records.json": json.dumps(records, indent=2) + "\n",
        root
        / "docs/SUPPORTED_HARDWARE.md": render_catalog_markdown(hardware["hardware"]),
        root
        / "docs/TESTED_HARDWARE.md": render_test_markdown(
            hardware["hardware"], records["records"]
        ),
    }
    lines = [
        "# Hardware validation catalog",
        "",
        "Generated from the model, setup, specification, run and "
        "assessment records. [Contribution guide](AGENTS.md).",
        "",
        "Catalogued devices and intended integration are distinct from "
        "reviewed physical validation.",
        "",
        "| Hardware | Kind | Integration | Runs | Reviewed runs |",
        "| --- | --- | --- | --- | --- |",
    ]
    for model in catalog["models"].values():
        runs = [r for r in catalog["runs"].values() if r["hardware_id"] == model["id"]]
        lines.append(
            f"| [{model['model']}](pages/{model['id']}.md) | "
            f"{model['kind']} | {model['integration']['status']} | "
            f"{len(runs)} | {sum(r['review_status'] != 'unreviewed' for r in runs)} |"
        )
        files[directory / "pages" / f"{model['id']}.md"] = render_model(model, catalog)
    lines.extend(
        [
            "",
            "Unknown execution/review identities and exact times are "
            "preserved explicitly. Migration does not constitute "
            "validation. See individual runs for failures, specification "
            "criteria, firmware, wiring, measurements and evidence.",
            "",
        ]
    )
    files[directory / "INDEX.md"] = "\n".join(lines)
    return files


def check_history(base_commit, root=ROOT):
    if not re.fullmatch(r"[0-9a-f]{40}", base_commit):
        raise ValueError("History check requires the full base commit SHA")
    prefix = "docs/hardware_catalog/"
    command = ["git", "-c", f"safe.directory={root.resolve()}", "-C", str(root)]
    paths = subprocess.run(
        command + ["ls-tree", "-r", "--name-only", base_commit, "--", prefix],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    for name in paths:
        relative = name[len(prefix) :]
        protected = relative.split("/", 1)[0] in {
            "models",
            "setups",
            "specifications",
            "runs",
            "assessments",
            "evidence",
        }
        if not protected:
            continue
        old = subprocess.run(
            command + ["show", f"{base_commit}:{name}"], check=True, capture_output=True
        ).stdout
        current = root / name
        if not current.is_file():
            raise ValueError(
                f"Historical catalog record/evidence removed: {name}; retain it "
                f"and add a superseding record"
            )
        new = current.read_bytes()
        if relative.split("/", 1)[0] == "models":
            previous, updated = json.loads(old), read_json(current)
            if any(
                previous[key] != updated[key]
                for key in ("id", "kind", "manufacturer", "model")
            ):
                raise ValueError(
                    f"Historical model identity changed: {name}; "
                    "register a new model ID"
                )
            continue
        same = (
            json.loads(old) == read_json(current)
            if name.endswith(".json")
            else old == new
        )
        if not same:
            raise ValueError(
                f"Historical catalog record/evidence changed: {name}; add a new "
                f"version or superseding record"
            )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Validate, export, query or scaffold hardware catalog records "
        "without hardware access"
    )
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--check", action="store_true")
    modes.add_argument("--write-docs", action="store_true")
    modes.add_argument("--query", choices=COLLECTIONS)
    modes.add_argument("--check-history")
    modes.add_argument(
        "--new", choices=("model", "setup", "specification", "run", "assessment")
    )
    parser.add_argument(
        "--kind",
        default="board",
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
        if args.check_history:
            check_history(args.check_history, workspace)
            print("Historical catalog records and evidence are preserved.")
            return 0
        if args.new:
            if (
                not args.output
                or not args.id
                or not re.fullmatch("[a-z][a-z0-9_-]{0,127}", args.id)
            ):
                parser.error("--new requires --id and a new --output file")
            template = read_json(
                directory
                / "templates"
                / f"{args.kind if args.new == 'model' else args.new}.json"
            )
            template["id"] = args.id
            with args.output.open("x", encoding="utf-8") as stream:
                stream.write(json.dumps(template, indent=2) + "\n")
            print(
                "Draft created; replace all placeholders, validate and review "
                "before contribution."
            )
            return 0
        if args.query:
            filters = {"entity": args.query, "limit": args.limit}
            filters.update(
                {
                    key: value
                    for key, value in [
                        ("hardware_id", args.hardware_id),
                        ("query", args.text),
                        ("cursor", args.cursor),
                    ]
                    if value is not None
                }
            )
            report = query_catalog(directory, **filters)
            print(json.dumps(report, indent=2))
            return 0 if report["status_code"] == "OK" else 1
        catalog = load_catalog(directory, workspace)
        for path, expected in generated_files(catalog, directory, workspace).items():
            if args.write_docs:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(expected, encoding="utf-8")
            elif path.read_text(encoding="utf-8") != expected:
                raise ValueError(
                    f"Regenerate {path.relative_to(workspace)} with --write-docs"
                )
        print(
            "Hardware catalog records, references, evidence checksums and "
            "generated exports are valid."
        )
        return 0
    except (
        OSError,
        ValueError,
        TypeError,
        OverflowError,
        subprocess.CalledProcessError,
    ) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    sys.exit(main())
