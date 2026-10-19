"""One read-only MCP tool for Joshua configuration integrity."""

import argparse
import asyncio
import json
import os
import stat
import sys

from mcp import types
from mcp.server.lowlevel import NotificationOptions, Server
from mcp.server.models import InitializationOptions
from mcp.server.stdio import stdio_server

LIMITATIONS = ["Implementation support and hardware readiness were not checked."]
REPORT_KEYS = frozenset(
    {
        "schema_version",
        "scope",
        "config_path",
        "stage",
        "result",
        "status_code",
        "message",
        "limitations",
    }
)
STATUS_CODES = frozenset(
    {
        "OK",
        "CANCELLED",
        "UNKNOWN",
        "INVALID_ARGUMENT",
        "DEADLINE_EXCEEDED",
        "NOT_FOUND",
        "ALREADY_EXISTS",
        "PERMISSION_DENIED",
        "RESOURCE_EXHAUSTED",
        "FAILED_PRECONDITION",
        "ABORTED",
        "OUT_OF_RANGE",
        "UNIMPLEMENTED",
        "INTERNAL",
        "UNAVAILABLE",
        "DATA_LOSS",
        "UNAUTHENTICATED",
    }
)
INPUT_SCHEMA = {"type": "object", "properties": {}, "additionalProperties": False}
REPORT_SCHEMA = {
    "type": "object",
    "properties": {
        "schema_version": {"type": "integer", "const": 1},
        "scope": {"type": "string", "const": "config_integrity"},
        "config_path": {"type": ["string", "null"]},
        "stage": {
            "type": "string",
            "enum": ["arguments", "input", "load", "integrity", "adapter"],
        },
        "result": {"type": "string", "enum": ["valid", "invalid", "not_run"]},
        "status_code": {"type": "string", "enum": sorted(STATUS_CODES)},
        "message": {"type": "string"},
        "limitations": {
            "type": "array",
            "items": {"type": "string"},
            "const": LIMITATIONS,
        },
    },
    "required": sorted(REPORT_KEYS),
    "additionalProperties": False,
}


def _unique_pairs(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("duplicate report key")
        value[key] = item
    return value


def _validate_report(report, exit_code, expected_path):
    if type(report) is not dict or set(report) != REPORT_KEYS:
        raise ValueError("wrong report fields")
    if type(report["schema_version"]) is not int or report["schema_version"] != 1:
        raise ValueError("wrong report version")
    if report["scope"] != "config_integrity" or type(report["scope"]) is not str:
        raise ValueError("wrong report scope")
    if report["config_path"] != expected_path or type(report["config_path"]) is not str:
        raise ValueError("wrong report path")
    if (
        type(report["message"]) is not str
        or len(report["message"].encode("utf-8")) > 8192
    ):
        raise ValueError("wrong report message")
    if report["limitations"] != LIMITATIONS or type(report["limitations"]) is not list:
        raise ValueError("wrong report limitations")
    if type(report["stage"]) is not str or type(report["result"]) is not str:
        raise ValueError("wrong report stage or result")
    status = report["status_code"]
    if type(status) is not str or status not in STATUS_CODES:
        raise ValueError("wrong report status")
    stage, result = report["stage"], report["result"]
    if stage == "integrity":
        if (exit_code, result, status) == (0, "valid", "OK"):
            return report
        if exit_code == 1 and result == "invalid" and status != "OK":
            return report
    elif (
        stage == "load"
        and exit_code == 1
        and result == "invalid"
        and status == "INVALID_ARGUMENT"
    ):
        return report
    elif stage == "input" and result == "not_run":
        if exit_code == 1 and status in {
            "INVALID_ARGUMENT",
            "NOT_FOUND",
            "PERMISSION_DENIED",
            "RESOURCE_EXHAUSTED",
        }:
            return report
        if exit_code == 3 and status == "INTERNAL":
            return report
    raise ValueError("inconsistent report status")


def parse_helper_report(raw, exit_code, expected_path):
    """Decode one complete helper line and enforce its stage/status/exit contract."""
    if not raw.endswith(b"\n") or raw.count(b"\n") != 1 or raw != raw.strip() + b"\n":
        raise ValueError("expected one JSON line")
    try:
        report = json.loads(raw.decode("utf-8"), object_pairs_hook=_unique_pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("invalid JSON report") from error
    return _validate_report(report, exit_code, expected_path)


def adapter_error(path, code, message):
    report = {
        "schema_version": 1,
        "scope": "config_integrity",
        "config_path": path,
        "stage": "adapter",
        "result": "not_run",
        "status_code": code,
        "message": message,
        "limitations": list(LIMITATIONS),
    }
    validate_adapter_report(report, path)
    return report


def validate_adapter_report(report, path):
    if type(report) is not dict or set(report) != REPORT_KEYS:
        raise ValueError("wrong adapter fields")
    if type(report["schema_version"]) is not int or report["schema_version"] != 1:
        raise ValueError("wrong adapter version")
    if report["scope"] != "config_integrity" or report["config_path"] != path:
        raise ValueError("wrong adapter scope or path")
    if (report["stage"], report["result"]) != ("adapter", "not_run"):
        raise ValueError("wrong adapter state")
    if report["status_code"] not in STATUS_CODES or report["status_code"] == "OK":
        raise ValueError("wrong adapter status")
    if type(report["message"]) is not str or report["limitations"] != LIMITATIONS:
        raise ValueError("wrong adapter message or limitations")
    return report


async def _read_capped(stream, limit):
    chunks = []
    count = 0
    while True:
        part = await stream.read(min(4096, limit + 1 - count))
        if not part:
            return b"".join(chunks)
        count += len(part)
        if count > limit:
            raise OverflowError("helper output exceeded cap")
        chunks.append(part)


class ValidationService:
    """Serializes helper calls and owns their full process lifecycle."""

    def __init__(self, config, validator, timeout=10.0):
        self.config = config
        self.validator = validator
        self.timeout = timeout
        self._busy = False
        self._poisoned = False
        self._active_call = None

    async def close(self):
        """Cancel an in-flight call and wait for its child cleanup on disconnect."""
        task = self._active_call
        if task is None or task.done() or task is asyncio.current_task():
            return
        task.cancel()
        while not task.done():
            try:
                await asyncio.wait_for(asyncio.shield(task), 5.0)
            except asyncio.CancelledError:
                # The active call's cancellation is expected after cleanup.
                pass
            except asyncio.TimeoutError:
                self._poisoned = True
                return

    async def _cleanup(self, spawn_task, process, readers):
        for task in readers:
            if not task.done():
                task.cancel()
        if readers:
            await asyncio.gather(*readers, return_exceptions=True)
        if spawn_task is not None and process is None:
            try:
                process = await asyncio.wait_for(asyncio.shield(spawn_task), 2.0)
            except asyncio.TimeoutError:
                # A later spawn still gets killed, while this service remains closed.
                def reap_later(task):
                    if not task.cancelled() and task.exception() is None:
                        child = task.result()
                        if child.returncode is None:
                            try:
                                child.kill()
                            except ProcessLookupError:
                                pass
                        asyncio.create_task(child.wait())

                spawn_task.add_done_callback(reap_later)
                return False
            except Exception:
                return True
        if process is not None and process.returncode is None:
            try:
                process.kill()
            except ProcessLookupError:
                pass
        if process is not None:
            try:
                await asyncio.wait_for(process.wait(), 2.0)
            except asyncio.TimeoutError:
                return False
        return True

    async def validate(self):
        if self._busy or self._poisoned:
            return adapter_error(
                self.config, "RESOURCE_EXHAUSTED", "Validation is already running."
            )
        self._busy = True
        self._active_call = asyncio.current_task()
        spawn_task = None
        process = None
        readers = []
        report = None
        cancelled = False
        try:

            async def run_child():
                nonlocal spawn_task, process, readers
                spawn_task = asyncio.create_task(
                    asyncio.create_subprocess_exec(
                        self.validator,
                        "--config",
                        self.config,
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.PIPE,
                    )
                )
                process = await asyncio.shield(spawn_task)
                readers = [
                    asyncio.create_task(_read_capped(process.stdout, 64 * 1024)),
                    asyncio.create_task(_read_capped(process.stderr, 64 * 1024)),
                    asyncio.create_task(process.wait()),
                ]
                return await asyncio.gather(*readers)

            stdout, _, exit_code = await asyncio.wait_for(run_child(), self.timeout)
            if exit_code < 0:
                raise ValueError("helper terminated by signal")
            report = parse_helper_report(stdout, exit_code, self.config)
        except asyncio.CancelledError:
            cancelled = True
        except asyncio.TimeoutError:
            report = adapter_error(
                self.config, "DEADLINE_EXCEEDED", "Validation timed out."
            )
        except OverflowError:
            report = adapter_error(
                self.config,
                "RESOURCE_EXHAUSTED",
                "Validation output exceeded the limit.",
            )
        except (OSError, PermissionError):
            report = adapter_error(
                self.config, "UNAVAILABLE", "Validation helper could not start."
            )
        except ValueError:
            report = adapter_error(
                self.config,
                "DATA_LOSS",
                "Validation helper returned an invalid report.",
            )
        finally:
            cleanup = asyncio.create_task(self._cleanup(spawn_task, process, readers))
            while not cleanup.done():
                try:
                    await asyncio.shield(cleanup)
                except asyncio.CancelledError:
                    cancelled = True
            if cleanup.exception() is not None or not cleanup.result():
                self._poisoned = True
                report = adapter_error(
                    self.config, "UNAVAILABLE", "Validation cleanup could not finish."
                )
            else:
                self._busy = False
            self._active_call = None
        if cancelled:
            raise asyncio.CancelledError
        return report


def make_server(service):
    app = Server("joshua-config-validation", version="0.1.0")

    @app.list_tools()
    async def list_tools():
        return [
            types.Tool(
                name="validate_config",
                description=(
                    "Check the operator-selected Joshua config for parser "
                    "and integrity errors."
                ),
                inputSchema=INPUT_SCHEMA,
                outputSchema=REPORT_SCHEMA,
                annotations=types.ToolAnnotations(
                    readOnlyHint=True,
                    destructiveHint=False,
                    idempotentHint=True,
                    openWorldHint=False,
                ),
            )
        ]

    @app.call_tool(validate_input=False)
    async def call_tool(name, arguments):
        if name != "validate_config":
            report = adapter_error(service.config, "INVALID_ARGUMENT", "Unknown tool.")
        elif arguments is None or arguments == {}:
            report = await service.validate()
        else:
            report = adapter_error(
                service.config, "INVALID_ARGUMENT", "Tool arguments must be empty."
            )
        if report["stage"] == "adapter":
            validate_adapter_report(report, service.config)
        else:
            # The helper report was checked against its actual exit in validate().
            # Check its shape once more before returning the SDK result.
            expected_exit = (
                3
                if report["stage"] == "input" and report["status_code"] == "INTERNAL"
                else 0 if report["result"] == "valid" else 1
            )
            _validate_report(report, expected_exit, service.config)
        encoded = json.dumps(report, ensure_ascii=True, separators=(",", ":"))
        return types.CallToolResult(
            content=[types.TextContent(type="text", text=encoded)],
            structuredContent=report,
            isError=report["result"] == "not_run",
        )

    return app


async def run_stdio(service):
    app = make_server(service)
    options = InitializationOptions(
        server_name="joshua-config-validation",
        server_version="0.1.0",
        capabilities=app.get_capabilities(NotificationOptions(), {}),
    )
    try:
        async with stdio_server() as (reader, writer):
            await app.run(reader, writer, options)
    finally:
        await service.close()


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Joshua configuration integrity MCP server"
    )
    parser.add_argument("--config", required=True)
    parser.add_argument("--validator", required=True)
    args = parser.parse_args(argv)
    config = os.path.abspath(args.config)
    validator = os.path.abspath(args.validator)
    try:
        info = os.stat(validator)
        if not stat.S_ISREG(info.st_mode) or not os.access(validator, os.X_OK):
            raise OSError("helper is not an executable regular file")
    except OSError as error:
        print(f"Validator is unavailable: {error}", file=sys.stderr)
        return 2
    asyncio.run(run_stdio(ValidationService(config, validator)))
    return 0
