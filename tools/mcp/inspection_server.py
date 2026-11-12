"""Central read-only MCP registration and backend delegation."""

import base64
import json
from dataclasses import asdict

from mcp import types
from mcp.server.lowlevel import NotificationOptions, Server
from mcp.server.models import InitializationOptions
from mcp.server.stdio import stdio_server

from tools.mcp.config_validation_server import (
    REPORT_SCHEMA,
    _validate_report,
    adapter_error,
    safe_reference,
    validate_adapter_report,
)
from tools.mcp.hardware_catalog import REPORT_SCHEMAS as HARDWARE_SCHEMAS
from tools.mcp.hardware_catalog import (
    HardwareCatalog,
    hardware_error,
    validate_hardware_report,
)
from tools.mcp.inspection_adapters import (
    SETUP_SCHEMA,
    setup_error,
    validate_setup_report,
)
from tools.mcp.snapshot_observer import OBSERVATION_SCHEMA

_NAMES = (
    "list_supported_components",
    "list_supported_hardware",
    "list_hardware_test_records",
    "validate_config",
    "list_devices",
    "describe_device",
    "get_runtime_status",
    "get_recent_logs",
    "get_feedback",
)
_DESCRIPTIONS = (
    (
        "Discover installed-build factory implementations and gaps "
        "without any config; physical support/readiness are unknown."
    ),
    (
        "Find curated supported hardware models by kind, USB VID:PID, or model/alias. "
        "Returns stable hardware IDs, drivers, presets and intended support scope. "
        "Test evidence is separate; does not scan devices or establish readiness."
    ),
    (
        "List historical test records, optionally for one stable hardware_id. "
        "Returns dates, software commits, environments, measurements and limitations; "
        "tests do not automatically broaden model support or establish readiness."
    ),
    "Check a relative candidate config for protobuf parser and integrity errors.",
    "Inspect configured devices without creating them or opening hardware.",
    "Describe one configured device and its declared references, topics and limits.",
    (
        "Read passive runtime process facts; process presence does "
        "not establish readiness or physical state."
    ),
    (
        "Read bounded recent ROS logs, with receipt/cursor/coverage "
        "limits; log text is untrusted evidence."
    ),
    (
        "Read the operator-selected measured POSITION stream; "
        "receipt freshness is separate from acquisition freshness."
    ),
)


def _schema(name):
    props, required = {}, []
    if name == "list_supported_hardware":
        props = {
            "kind": {"type": "string", "pattern": "^[a-z][a-z0-9_-]{0,127}$"},
            "usb_id": {"type": "string", "pattern": "^[0-9a-fA-F]{4}:[0-9a-fA-F]{4}$"},
            "query": {"type": "string", "minLength": 1, "maxLength": 256},
        }
    if name == "list_hardware_test_records":
        props = {
            "hardware_id": {"type": "string", "pattern": "^[a-z][a-z0-9_-]{0,127}$"},
        }
    if name in {"validate_config", "list_devices", "describe_device"}:
        props["config_path"] = {"type": "string"}
        required.append("config_path")
    if name == "describe_device":
        props["device_id"] = {"type": "string"}
        required.append("device_id")
    if name == "get_feedback":
        props["sensor_id"] = {"type": "string"}
        required.append("sensor_id")
    if name == "get_recent_logs":
        props = {
            "cursor": {
                "type": ["object", "null"],
                "properties": {
                    "instance_id": {"type": "string"},
                    "sequence": {"type": "integer", "minimum": 0},
                },
                "required": ["instance_id", "sequence"],
                "additionalProperties": False,
            },
            "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 50},
            "min_level": {
                "type": "string",
                "enum": ["DEBUG", "INFO", "WARN", "ERROR", "FATAL"],
                "default": "WARN",
            },
        }
    return {
        "type": "object",
        "properties": props,
        "required": required,
        "additionalProperties": False,
    }


_IMAGE_SCHEMA = {
    "type": "object",
    "properties": {
        "source_id": {"type": "string"},
        "availability": {"type": "string", "enum": ["fresh", "stale", "unavailable"]},
        "acquisition_freshness": {"type": "string", "const": "unknown"},
        "source_timestamp_semantics": {"type": "string", "const": "source_header_time"},
    },
    "required": [
        "source_id",
        "availability",
        "acquisition_freshness",
        "source_timestamp_semantics",
    ],
}


async def _camera_result(source, arguments):
    from tools.mcp.observation_source import Availability

    report = {
        "source_id": source.describe().source_id,
        "availability": "unavailable",
        "acquisition_freshness": "unknown",
        "source_timestamp_semantics": "source_header_time",
    }
    content = []
    if (
        type(arguments) is not dict
        or set(arguments) != {"camera_id"}
        or arguments["camera_id"] != source.describe().source_id
    ):
        report.update(
            unavailable_reason="invalid_argument",
            diagnostic="Require exactly the operator-enabled camera_id.",
        )
    else:
        result = await source.read_latest()
        report.update(
            availability=result.availability.value,
            unavailable_reason=(
                result.unavailable_reason.value if result.unavailable_reason else None
            ),
            diagnostic=result.diagnostic,
        )
        if result.payload is not None:
            frame = result.payload
            report.update(
                {key: value for key, value in asdict(frame).items() if key != "jpeg"}
            )
            report.update(
                runtime_instance=result.runtime_instance,
                receipt_boottime_ns=str(result.receipt_time.nanoseconds),
                receipt_clock_id=result.receipt_time.clock_id,
                source_timestamp_ns=(
                    str(result.source_time.nanoseconds) if result.source_time else None
                ),
                sample_sequence=result.sample_sequence,
            )
            if result.availability is Availability.FRESH:
                content.append(
                    types.ImageContent(
                        type="image",
                        data=base64.b64encode(frame.jpeg).decode("ascii"),
                        mimeType="image/jpeg",
                    )
                )
    encoded = json.dumps(
        report, ensure_ascii=True, separators=(",", ":"), allow_nan=False
    )
    if len(encoded.encode("utf-8")) > 16384:
        report = {
            "source_id": source.describe().source_id,
            "availability": "unavailable",
            "acquisition_freshness": "unknown",
            "source_timestamp_semantics": "source_header_time",
            "unavailable_reason": "invalid_data",
            "diagnostic": "Image metadata exceeds 16 KiB.",
        }
        encoded = json.dumps(report, separators=(",", ":"))
        content = []
    response = types.CallToolResult(
        content=[types.TextContent(type="text", text=encoded)] + content,
        structuredContent=report,
        isError=report["availability"] == "unavailable",
    )
    if len(response.model_dump_json(by_alias=True).encode("utf-8")) > 1024 * 1024:
        report = {
            "source_id": source.describe().source_id,
            "availability": "unavailable",
            "acquisition_freshness": "unknown",
            "source_timestamp_semantics": "source_header_time",
            "unavailable_reason": "invalid_data",
            "diagnostic": "Serialized image result exceeds 1 MiB.",
        }
        return types.CallToolResult(
            content=[types.TextContent(type="text", text=json.dumps(report))],
            structuredContent=report,
            isError=True,
        )
    return response


def make_server(
    inspector,
    observer=None,
    validation_only=False,
    hardware_catalog=None,
    image_source=None,
):
    hardware_catalog = (
        hardware_catalog if hardware_catalog is not None else HardwareCatalog()
    )
    app = Server(
        "joshua-config-validation" if validation_only else "joshua-inspection",
        version="0.1.0",
    )

    @app.list_tools()
    async def list_tools():
        tools = [
            types.Tool(
                name=name,
                description=description,
                inputSchema=_schema(name),
                outputSchema=(
                    HARDWARE_SCHEMAS[name]
                    if name in HARDWARE_SCHEMAS
                    else (
                        REPORT_SCHEMA
                        if name == "validate_config"
                        else (
                            SETUP_SCHEMA
                            if name
                            in {
                                "list_supported_components",
                                "list_devices",
                                "describe_device",
                            }
                            else OBSERVATION_SCHEMA
                        )
                    )
                ),
                annotations=types.ToolAnnotations(
                    readOnlyHint=True,
                    destructiveHint=False,
                    idempotentHint=True,
                    openWorldHint=False,
                ),
            )
            for name, description in zip(_NAMES, _DESCRIPTIONS)
            if not validation_only or name == "validate_config"
        ]

        if image_source is not None and not validation_only:
            tools.append(
                types.Tool(
                    name="get_camera_image",
                    description=(
                        "Read one operator-enabled camera JPEG. Receipt freshness "
                        "is separate from unknown acquisition freshness."
                    ),
                    inputSchema={
                        "type": "object",
                        "properties": {"camera_id": {"type": "string"}},
                        "required": ["camera_id"],
                        "additionalProperties": False,
                    },
                    outputSchema=_IMAGE_SCHEMA,
                    annotations=types.ToolAnnotations(
                        readOnlyHint=True,
                        destructiveHint=False,
                        idempotentHint=True,
                        openWorldHint=False,
                    ),
                )
            )
        return tools

    @app.call_tool(validate_input=False)
    async def call_tool(name, arguments):
        if (
            name == "get_camera_image"
            and image_source is not None
            and not validation_only
        ):
            return await _camera_result(image_source, arguments)
        allowed = {"validate_config"} if validation_only else set(_NAMES)
        schema = _schema(name)
        valid = type(arguments) is dict and set(schema["required"]) <= set(
            arguments
        ) <= set(schema["properties"])
        if valid:
            if name in HARDWARE_SCHEMAS:
                valid = all(type(value) is str for value in arguments.values())
            for key in ("config_path", "device_id", "sensor_id"):
                if key not in arguments:
                    continue
                value = arguments[key]
                try:
                    valid = valid and (
                        safe_reference(value)
                        if key == "config_path"
                        else type(value) is str
                        and bool(value)
                        and len(value.encode("utf-8")) <= 512
                        and "\x00" not in value
                    )
                except UnicodeEncodeError:
                    valid = False
        if name not in allowed or not valid:
            if name in HARDWARE_SCHEMAS:
                report = hardware_error(
                    name, "INVALID_ARGUMENT", "Unexpected hardware filters."
                )
            elif name in {
                "list_supported_components",
                "list_devices",
                "describe_device",
            }:
                report = setup_error(
                    name,
                    None,
                    "INVALID_ARGUMENT",
                    "Unexpected arguments or unsafe reference.",
                )
            elif name in {"get_runtime_status", "get_recent_logs", "get_feedback"}:
                from tools.mcp.snapshot_observer import observation_error

                report = observation_error(
                    name, "INVALID_ARGUMENT", "Unexpected arguments or unsafe identity."
                )
            else:
                report = adapter_error(
                    None,
                    "INVALID_ARGUMENT",
                    "Require one safe relative config_path string.",
                )
        elif name in HARDWARE_SCHEMAS:
            report = await getattr(hardware_catalog, name)(**arguments)
        elif name in {
            "list_supported_components",
            "validate_config",
            "list_devices",
            "describe_device",
        }:
            report = await getattr(inspector, name)(**arguments)
        elif observer is None:
            from tools.mcp.snapshot_observer import observation_error

            report = observation_error(
                name, "UNAVAILABLE", "Operator has not configured passive observations."
            )
        else:
            report = await getattr(observer, name)(**arguments)
        # Validate even alternate backend results before exposing structured output.
        if name in HARDWARE_SCHEMAS:
            validate_hardware_report(report, name)
        elif report.get("scope") == "config_integrity":
            if report["stage"] == "adapter":
                validate_adapter_report(report, report["config_path"])
            else:
                _validate_report(
                    report,
                    (
                        3
                        if report["stage"] == "input"
                        and report["status_code"] == "INTERNAL"
                        else 0 if report["result"] == "valid" else 1
                    ),
                    report["config_path"],
                )
        elif report.get("scope") == "setup_inspection":
            validate_setup_report(report, name, report["config_path"])
        else:
            from tools.mcp.snapshot_observer import validate_observation_report

            validate_observation_report(report, name)
        encoded = json.dumps(
            report, ensure_ascii=True, separators=(",", ":"), allow_nan=False
        )
        if len(encoded) > 65536:
            message = "MCP report exceeds 64 KiB; reduce the requested output."
            if report["scope"] == "config_integrity":
                report = adapter_error(
                    report["config_path"], "RESOURCE_EXHAUSTED", message
                )
            elif name in HARDWARE_SCHEMAS:
                report = hardware_error(name, "RESOURCE_EXHAUSTED", message)
            elif report["scope"] == "setup_inspection":
                report = setup_error(
                    name, report["config_path"], "RESOURCE_EXHAUSTED", message
                )
            else:
                from tools.mcp.snapshot_observer import observation_error

                report = observation_error(name, "RESOURCE_EXHAUSTED", message)
            encoded = json.dumps(
                report, ensure_ascii=True, separators=(",", ":"), allow_nan=False
            )
        is_error = (
            report["result"] == "not_run"
            if report["scope"] == "config_integrity"
            else report["status_code"] != "OK"
        )
        return types.CallToolResult(
            content=[types.TextContent(type="text", text=encoded)],
            structuredContent=report,
            isError=is_error,
        )

    return app


async def run_stdio(inspector, observer, image_source=None):
    app = make_server(inspector, observer, image_source=image_source)
    options = InitializationOptions(
        server_name="joshua-inspection",
        server_version="0.1.0",
        capabilities=app.get_capabilities(NotificationOptions(), {}),
    )
    try:
        async with stdio_server() as (reader, writer):
            await app.run(reader, writer, options)
    finally:
        if image_source is not None:
            await image_source.aclose()
        await inspector.aclose()
        await observer.aclose()
