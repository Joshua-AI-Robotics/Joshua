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
RESTART_MESSAGE = (
    "Validation cleanup could not finish. Operator must restart the server."
)
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
INPUT_SCHEMA = {
    "type": "object",
    "properties": {"config_path": {"type": "string"}},
    "required": ["config_path"],
    "additionalProperties": False,
}
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
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError) as error:
        raise ValueError("invalid JSON report") from error
    return _validate_report(report, exit_code, expected_path)


def safe_reference(path):
    if type(path) is not str or not path or path.startswith("/") or "\x00" in path:
        return False
    try:
        encoded = path.encode("utf-8")
    except UnicodeEncodeError:
        return False
    return len(encoded) <= 4096 and all(
        component not in ("", ".", "..") for component in path.split("/")
    )


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
    if path is not None and not safe_reference(path):
        raise ValueError("wrong adapter path")
    if (report["stage"], report["result"]) != ("adapter", "not_run"):
        raise ValueError("wrong adapter state")
    if report["status_code"] not in STATUS_CODES or report["status_code"] == "OK":
        raise ValueError("wrong adapter status")
    if type(report["message"]) is not str or report["limitations"] != LIMITATIONS:
        raise ValueError("wrong adapter message or limitations")
    try:
        message_size = len(report["message"].encode("utf-8"))
    except UnicodeEncodeError as error:
        raise ValueError("wrong adapter message encoding") from error
    if message_size > 8192:
        raise ValueError("wrong adapter message length")
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


async def _discard(stream):
    while await stream.read(4096):
        pass


async def _kill_drain_wait(process, deadline):
    if process.returncode is None:
        try:
            process.kill()
        except ProcessLookupError:
            pass
    tasks = [
        asyncio.create_task(_discard(process.stdout)),
        asyncio.create_task(_discard(process.stderr)),
        asyncio.create_task(process.wait()),
    ]
    try:
        await asyncio.wait_for(
            asyncio.gather(*tasks), max(0, deadline - asyncio.get_running_loop().time())
        )
        return True
    except asyncio.TimeoutError:
        return False
    finally:
        for task in tasks:
            if not task.done():
                task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)


class ValidationService:
    """Serializes helper calls and owns their full process lifecycle."""

    def __init__(self, config_root, validator, timeout=10.0):
        self.validator = validator
        self.timeout = timeout
        self._root_fd = os.open(config_root, os.O_PATH | os.O_DIRECTORY | os.O_CLOEXEC)
        self._busy = False
        self._poisoned = False
        self._closed = False
        self._active_call = None

    async def close(self):
        """Cancel an in-flight call and wait for its child cleanup on disconnect."""
        if not self._closed:
            self._closed = True
            os.close(self._root_fd)
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
        deadline = asyncio.get_running_loop().time() + 2.0
        if process is not None and process.returncode is None:
            try:
                process.kill()
            except ProcessLookupError:
                pass
        for task in readers:
            if not task.done():
                task.cancel()
        if readers:
            try:
                await asyncio.wait_for(
                    asyncio.gather(*readers, return_exceptions=True),
                    max(0, deadline - asyncio.get_running_loop().time()),
                )
            except asyncio.TimeoutError:
                return False
        if spawn_task is not None and process is None:
            try:
                process = await asyncio.wait_for(
                    asyncio.shield(spawn_task),
                    max(0, deadline - asyncio.get_running_loop().time()),
                )
            except asyncio.TimeoutError:
                # Keep the service unavailable after an unconfirmed late spawn.
                def reap_later(task):
                    if task.cancelled() or task.exception() is not None:
                        return
                    child = task.result()
                    reaper = asyncio.create_task(
                        _kill_drain_wait(child, asyncio.get_running_loop().time() + 2.0)
                    )
                    reaper.add_done_callback(
                        lambda finished: (
                            finished.exception() if not finished.cancelled() else None
                        )
                    )

                spawn_task.add_done_callback(reap_later)
                return False
            except Exception:
                return True
        if process is not None:
            return await _kill_drain_wait(process, deadline)
        return True

    async def validate(self, config_path):
        accepted = config_path if safe_reference(config_path) else None
        if self._poisoned:
            return adapter_error(
                accepted,
                "UNAVAILABLE",
                RESTART_MESSAGE,
            )
        if self._closed:
            return adapter_error(
                accepted, "UNAVAILABLE", "Validation server is closed."
            )
        if accepted is None:
            return adapter_error(
                None,
                "INVALID_ARGUMENT",
                "config_path must be a safe relative POSIX path.",
            )
        if self._busy:
            return adapter_error(
                accepted, "RESOURCE_EXHAUSTED", "Validation is already running."
            )
        self._busy = True
        self._active_call = asyncio.current_task()
        spawn_task = None
        process = None
        readers = []
        report = None
        cancelled = False
        root_fd = None
        try:
            # The duplicate belongs to the spawn coroutine through its actual
            # subprocess creation, even if the outer deadline expires first.
            root_fd = os.dup(self._root_fd)

            async def spawn_child():
                try:
                    return await asyncio.create_subprocess_exec(
                        self.validator,
                        "--config-root-fd",
                        str(root_fd),
                        "--config",
                        accepted,
                        pass_fds=(root_fd,),
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.PIPE,
                    )
                finally:
                    os.close(root_fd)

            async def run_child():
                nonlocal spawn_task, process, readers
                spawn_task = asyncio.create_task(spawn_child())
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
            report = parse_helper_report(stdout, exit_code, accepted)
        except asyncio.CancelledError:
            cancelled = True
        except asyncio.TimeoutError:
            report = adapter_error(
                accepted, "DEADLINE_EXCEEDED", "Validation timed out."
            )
        except OverflowError:
            report = adapter_error(
                accepted, "RESOURCE_EXHAUSTED", "Validation output exceeded the limit."
            )
        except OSError:
            report = adapter_error(
                accepted, "UNAVAILABLE", "Validation helper could not start."
            )
        except ValueError:
            report = adapter_error(
                accepted, "DATA_LOSS", "Validation helper returned an invalid report."
            )
        finally:
            # A duplicate not yet handed to a spawn task remains ours to close.
            if spawn_task is None and root_fd is not None:
                os.close(root_fd)
            cleanup = asyncio.create_task(self._cleanup(spawn_task, process, readers))
            while not cleanup.done():
                try:
                    await asyncio.shield(cleanup)
                except asyncio.CancelledError:
                    cancelled = True
                except Exception:
                    break
            if (
                cleanup.cancelled()
                or cleanup.exception() is not None
                or not cleanup.result()
            ):
                self._poisoned = True
                report = adapter_error(
                    accepted,
                    "UNAVAILABLE",
                    RESTART_MESSAGE,
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
                    "Check a relative Joshua config under the operator-selected "
                    "root for parser and integrity errors."
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
            report = adapter_error(None, "INVALID_ARGUMENT", "Unknown tool.")
        elif (
            type(arguments) is not dict
            or set(arguments) != {"config_path"}
            or not safe_reference(arguments["config_path"])
        ):
            report = adapter_error(
                None,
                "INVALID_ARGUMENT",
                "Require one safe relative config_path string.",
            )
        else:
            report = await service.validate(arguments["config_path"])
        expected_path = report["config_path"]
        if report["stage"] == "adapter":
            validate_adapter_report(report, expected_path)
        else:
            # The helper report was checked against its actual exit in validate().
            expected_exit = (
                3
                if report["stage"] == "input" and report["status_code"] == "INTERNAL"
                else 0 if report["result"] == "valid" else 1
            )
            _validate_report(report, expected_exit, expected_path)
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
    parser.add_argument("--config-root", default=os.getcwd())
    parser.add_argument("--validator", required=True)
    args = parser.parse_args(argv)
    root = os.path.abspath(args.config_root)
    validator = os.path.abspath(args.validator)
    try:
        root.encode("utf-8")
        validator.encode("utf-8")
    except UnicodeEncodeError:
        print("Paths must be UTF-8 encodable.", file=sys.stderr)
        return 2
    try:
        info = os.stat(validator)
        if not stat.S_ISREG(info.st_mode) or not os.access(validator, os.X_OK):
            raise OSError("helper is not an executable regular file")
    except OSError as error:
        print(f"Validator is unavailable: {error}", file=sys.stderr)
        return 2
    try:
        service = ValidationService(root, validator)
    except OSError as error:
        print(f"Config root is unavailable: {error}", file=sys.stderr)
        return 2
    asyncio.run(run_stdio(service))
    return 0
