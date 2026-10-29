"""Central read-only MCP registration and backend delegation."""

import json

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
from tools.mcp.inspection_adapters import (
    SETUP_SCHEMA,
    setup_error,
    validate_setup_report,
)
from tools.mcp.snapshot_observer import OBSERVATION_SCHEMA

_NAMES = (
    "list_supported_components",
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


def make_server(inspector, observer=None, validation_only=False):
    app = Server(
        "joshua-config-validation" if validation_only else "joshua-inspection",
        version="0.1.0",
    )

    @app.list_tools()
    async def list_tools():
        return [
            types.Tool(
                name=name,
                description=description,
                inputSchema=_schema(name),
                outputSchema=(
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

    @app.call_tool(validate_input=False)
    async def call_tool(name, arguments):
        allowed = {"validate_config"} if validation_only else set(_NAMES)
        schema = _schema(name)
        valid = type(arguments) is dict and set(schema["required"]) <= set(
            arguments
        ) <= set(schema["properties"])
        if valid:
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
            if name in {"list_supported_components", "list_devices", "describe_device"}:
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
        if report.get("scope") == "config_integrity":
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


async def run_stdio(inspector, observer):
    app = make_server(inspector, observer)
    options = InitializationOptions(
        server_name="joshua-inspection",
        server_version="0.1.0",
        capabilities=app.get_capabilities(NotificationOptions(), {}),
    )
    try:
        async with stdio_server() as (reader, writer):
            await app.run(reader, writer, options)
    finally:
        await inspector.aclose()
        await observer.aclose()
