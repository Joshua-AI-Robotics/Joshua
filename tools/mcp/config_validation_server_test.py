import asyncio
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client

from tools.mcp import config_validation_server as server

RUNFILES = Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"]
SERVER = RUNFILES / "tools/mcp/config_validation_server"
VALIDATOR = RUNFILES / "config/joshua_validate_config"
LIMITATIONS = ["Implementation support and hardware readiness were not checked."]


def helper_report(path, **changes):
    report = dict(
        schema_version=1,
        scope="config_integrity",
        config_path=str(path),
        stage="integrity",
        result="valid",
        status_code="OK",
        message="fixture",
        limitations=LIMITATIONS,
    )
    report.update(changes)
    return report


def fake_helper(directory, body):
    helper = Path(directory) / "validator"
    helper.write_text("#!/usr/bin/env python3\n" + body)
    helper.chmod(0o700)
    return helper


class ReportContractTest(unittest.TestCase):
    def test_strict_report(self):
        path = "/tmp/config.pbtxt"
        good = helper_report(path)
        self.assertEqual(
            server.parse_helper_report(json.dumps(good).encode() + b"\n", 0, path), good
        )
        for bad, code in [
            ({**good, "schema_version": True}, 0),
            ({**good, "extra": 1}, 0),
            (
                {
                    **good,
                    "stage": "integrity",
                    "result": "invalid",
                    "status_code": "OK",
                },
                1,
            ),
            (good, 1),
        ]:
            with self.subTest(bad=bad, code=code), self.assertRaises(ValueError):
                server.parse_helper_report(json.dumps(bad).encode() + b"\n", code, path)
        duplicate = json.dumps(good).replace(
            '"schema_version": 1,', '"schema_version": 1, "schema_version": 1,'
        )
        with self.assertRaises(ValueError):
            server.parse_helper_report(duplicate.encode() + b"\n", 0, path)


class ServiceTest(unittest.IsolatedAsyncioTestCase):
    async def test_large_burst_overflow_reaps_and_recovers(self):
        for fd in (1, 2):
            with self.subTest(fd=fd), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "config.pbtxt"
                path.write_text("")
                pidfile = Path(directory) / "pid"
                helper = fake_helper(
                    directory,
                    "import os, time\n"
                    f"open({str(pidfile)!r}, 'w').write(str(os.getpid()))\n"
                    f"os.write({fd}, b'x' * 1048576)\n"
                    "time.sleep(30)\n",
                )
                service = server.ValidationService(str(path.parent), str(helper))
                self.addAsyncCleanup(service.close)
                report = await asyncio.wait_for(service.validate("config.pbtxt"), 8)
                self.assertEqual(report["status_code"], "RESOURCE_EXHAUSTED")
                with self.assertRaises(ProcessLookupError):
                    os.kill(int(pidfile.read_text()), 0)
                helper.write_text(
                    "#!/usr/bin/env python3\n"
                    "import json\n"
                    "print(json.dumps(" + repr(helper_report("config.pbtxt")) + "))\n"
                )
                report = await asyncio.wait_for(service.validate("config.pbtxt"), 8)
                self.assertEqual(report["result"], "valid", report)

    async def test_late_spawn_burst_drains_pipes_and_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.pbtxt"
            path.write_text("")
            pidfile = Path(directory) / "pid"
            helper = fake_helper(
                directory,
                "import os, time\n"
                f"open({str(pidfile)!r}, 'w').write(str(os.getpid()))\n"
                "os.write(1, b'x' * 1048576)\n"
                "time.sleep(30)\n",
            )
            service = server.ValidationService(
                str(path.parent), str(helper), timeout=0.1
            )
            self.addAsyncCleanup(service.close)
            entered, release = asyncio.Event(), asyncio.Event()
            spawned = []
            original = asyncio.create_subprocess_exec

            async def delayed_spawn(*args, **kwargs):
                child = await original(*args, **kwargs)
                spawned.append(child)
                entered.set()
                await release.wait()
                return child

            with mock.patch.object(
                server.asyncio, "create_subprocess_exec", delayed_spawn
            ):
                pending = asyncio.create_task(service.validate("config.pbtxt"))
                await entered.wait()
                self.assertEqual((await pending)["status_code"], "UNAVAILABLE")
                self.assertEqual(
                    (await service.validate("config.pbtxt"))["status_code"],
                    "UNAVAILABLE",
                )
                release.set()
            for _ in range(300):
                try:
                    os.kill(int(pidfile.read_text()), 0)
                except ProcessLookupError:
                    break
                await asyncio.sleep(0.01)
            else:
                self.fail("late-spawn helper survived cleanup")
            for _ in range(300):
                if spawned[0].stdout.at_eof() and spawned[0].stderr.at_eof():
                    break
                await asyncio.sleep(0.01)
            else:
                self.fail("late-spawn helper pipes were not drained")
            self.assertEqual(
                (await service.validate("config.pbtxt"))["status_code"], "UNAVAILABLE"
            )

    async def test_rejects_non_utf8_startup_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.pbtxt"
            path.write_text("")
            bad_path = os.fsdecode(b"/tmp/\xff")
            for flag, config, validator in (
                ("--config-root", bad_path, str(VALIDATOR)),
                ("--validator", str(path.parent), bad_path),
            ):
                with self.subTest(flag=flag):
                    process = await asyncio.create_subprocess_exec(
                        str(SERVER),
                        "--validator",
                        validator,
                        "--config-root",
                        config,
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.PIPE,
                    )
                    stdout, stderr = await asyncio.wait_for(process.communicate(), 3)
                    self.assertEqual(process.returncode, 2)
                    self.assertEqual(stdout, b"")
                    self.assertIn(b"UTF-8", stderr)
                    self.assertNotIn(b"Traceback", stderr)
                    self.assertNotIn(b"\\udcff", stderr)
            unicode_path = Path(directory) / "config-é.pbtxt"
            unicode_path.mkdir()
            process = await asyncio.create_subprocess_exec(
                str(SERVER),
                "--validator",
                str(VALIDATOR),
                "--config-root",
                str(unicode_path),
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            stdout, _ = await asyncio.wait_for(process.communicate(), 3)
            self.assertEqual(process.returncode, 0)
            self.assertEqual(stdout, b"")

    async def test_deep_helper_json_is_structured_data_loss_over_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.pbtxt"
            path.write_text("")
            helper = fake_helper(
                directory,
                "import sys\n"
                "sys.stdout.write('[' * 2000 + '0' + ']' * 2000 + '\\n')\n",
            )
            params = StdioServerParameters(
                command=str(SERVER),
                args=["--validator", str(helper), "--config-root", str(path.parent)],
            )
            async with stdio_client(params) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    await client.initialize()
                    malformed = await client.call_tool(
                        "validate_config", {"config_path": "config.pbtxt"}
                    )
                    self.assertTrue(malformed.isError)
                    self.assertEqual(
                        malformed.structuredContent["status_code"], "DATA_LOSS"
                    )
                    helper.write_text(
                        "#!/usr/bin/env python3\n"
                        "import json\n"
                        "print(json.dumps("
                        + repr(helper_report("config.pbtxt"))
                        + "))\n"
                    )
                    valid = await client.call_tool(
                        "validate_config", {"config_path": "config.pbtxt"}
                    )
                    self.assertFalse(valid.isError, valid.model_dump_json())
                    self.assertEqual(valid.structuredContent["result"], "valid")

    async def test_real_sdk_stdio_exchange(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.pbtxt"
            path.write_text("")
            direct_service = server.ValidationService(str(path.parent), str(VALIDATOR))
            self.addAsyncCleanup(direct_service.close)
            direct = await direct_service.validate("config.pbtxt")
            self.assertEqual(direct["result"], "valid", direct)
            # The SDK defaults to a restricted environment. Keep the container's
            # ROS library paths when it starts the Bazel server from this test.
            params = StdioServerParameters(
                command=str(SERVER),
                args=["--validator", str(VALIDATOR), "--config-root", str(path.parent)],
                env=os.environ.copy(),
            )
            async with stdio_client(params) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    init = await client.initialize()
                    self.assertEqual(str(init.protocolVersion), "2025-11-25")
                    tools = (await client.list_tools()).tools
                    self.assertEqual([tool.name for tool in tools], ["validate_config"])
                    self.assertEqual(
                        tools[0].inputSchema,
                        {
                            "type": "object",
                            "properties": {"config_path": {"type": "string"}},
                            "required": ["config_path"],
                            "additionalProperties": False,
                        },
                    )
                    self.assertTrue(tools[0].annotations.readOnlyHint)
                    self.assertTrue(tools[0].annotations.idempotentHint)
                    self.assertFalse(tools[0].annotations.destructiveHint)
                    self.assertFalse(tools[0].annotations.openWorldHint)
                    self.assertIn(
                        "adapter", tools[0].outputSchema["properties"]["stage"]["enum"]
                    )
                    valid = await client.call_tool(
                        "validate_config", {"config_path": "config.pbtxt"}
                    )
                    self.assertFalse(valid.isError, valid.model_dump_json())
                    self.assertEqual(valid.structuredContent["result"], "valid")
                    self.assertEqual(
                        json.loads(valid.content[0].text), valid.structuredContent
                    )
                    path.write_text("unknown_field: 1")
                    invalid = await client.call_tool(
                        "validate_config", {"config_path": "config.pbtxt"}
                    )
                    self.assertFalse(invalid.isError)
                    self.assertEqual(invalid.structuredContent["result"], "invalid")
                    path.unlink()
                    missing = await client.call_tool(
                        "validate_config", {"config_path": "config.pbtxt"}
                    )
                    self.assertTrue(missing.isError)
                    self.assertEqual(missing.structuredContent["stage"], "input")
                    bad = await client.call_tool("validate_config", {"path": str(path)})
                    self.assertTrue(bad.isError)
                    self.assertIsNone(bad.structuredContent["config_path"])
                    self.assertTrue((await client.call_tool("unknown", {})).isError)
                    for bad_args in (
                        {},
                        {"config_path": 7},
                        {"config_path": "../escape"},
                        {"config_path": "config.pbtxt", "validator": "other"},
                        {"config_path": "x" * 4097},
                    ):
                        with self.subTest(bad_args=bad_args):
                            bad = await client.call_tool("validate_config", bad_args)
                            self.assertTrue(bad.isError)
                            self.assertIsNone(bad.structuredContent["config_path"])

    async def test_sdk_uses_request_local_paths_and_parser_detail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            params = StdioServerParameters(
                command=str(SERVER),
                args=["--validator", str(VALIDATOR)],
                cwd=directory,
                env=os.environ.copy(),
            )
            async with stdio_client(params) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    await client.initialize()
                    tools = (await client.list_tools()).tools
                    self.assertIn("config_path", tools[0].inputSchema["required"])
                    self.assertFalse((root / "first.pbtxt").exists())
                    self.assertFalse((root / "second.pbtxt").exists())
                    (root / "first.pbtxt").write_text("")
                    (root / "second.pbtxt").write_text("\nunknown_field: 1\n")
                    first = await client.call_tool(
                        "validate_config", {"config_path": "first.pbtxt"}
                    )
                    second = await client.call_tool(
                        "validate_config", {"config_path": "second.pbtxt"}
                    )
                    self.assertFalse(first.isError)
                    self.assertFalse(second.isError)
                    self.assertEqual(
                        first.structuredContent["config_path"], "first.pbtxt"
                    )
                    self.assertEqual(
                        second.structuredContent["config_path"], "second.pbtxt"
                    )
                    self.assertIn(
                        "line 2, column 1", second.structuredContent["message"]
                    )
                    self.assertIn("unknown_field", second.structuredContent["message"])
                    self.assertEqual(
                        json.loads(second.content[0].text), second.structuredContent
                    )

    async def test_poisoned_cleanup_requires_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            helper = fake_helper(directory, "print('unused')\n")
            for cleanup_result in (False, RuntimeError("cleanup failed")):
                with self.subTest(cleanup_result=cleanup_result):
                    service = server.ValidationService(directory, str(helper))
                    self.addAsyncCleanup(service.close)
                    if cleanup_result is False:
                        cleanup_patch = mock.patch.object(
                            service, "_cleanup", return_value=False
                        )
                    else:
                        cleanup_patch = mock.patch.object(
                            service, "_cleanup", side_effect=cleanup_result
                        )
                    with cleanup_patch, mock.patch.object(
                        server.asyncio,
                        "create_subprocess_exec",
                        side_effect=OSError("blocked"),
                    ) as spawn:
                        first = await service.validate("one.pbtxt")
                        second = await service.validate("two.pbtxt")
                        self.assertEqual(first["status_code"], "UNAVAILABLE")
                        self.assertEqual(second["status_code"], "UNAVAILABLE")
                        self.assertIn("restart", first["message"].lower())
                        self.assertIn("restart", second["message"].lower())
                        self.assertEqual(second["config_path"], "two.pbtxt")
                        self.assertEqual(spawn.call_count, 1)
                    await service.close()

    async def test_duplication_failure_is_recoverable(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "config.pbtxt").write_text("")
            service = server.ValidationService(directory, str(VALIDATOR))
            self.addAsyncCleanup(service.close)
            with mock.patch.object(
                server.os, "dup", side_effect=OSError("fd unavailable")
            ):
                failed = await service.validate("config.pbtxt")
            self.assertEqual(
                (failed["status_code"], failed["config_path"]),
                ("UNAVAILABLE", "config.pbtxt"),
            )
            recovered = await service.validate("config.pbtxt")
            self.assertEqual(
                (recovered["result"], recovered["status_code"]), ("valid", "OK")
            )

    async def test_untransferred_duplicate_closes_before_spawn(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "config.pbtxt").write_text("")
            service = server.ValidationService(directory, str(VALIDATOR), timeout=0)
            self.addAsyncCleanup(service.close)
            original_dup = os.dup
            duplicates = []

            def tracked_dup(fd):
                duplicate = original_dup(fd)
                duplicates.append(duplicate)
                return duplicate

            async def timeout_before_run_child(coroutine, timeout):
                coroutine.close()
                raise asyncio.TimeoutError

            with mock.patch.object(
                server.os, "dup", side_effect=tracked_dup
            ), mock.patch.object(
                server.asyncio, "wait_for", side_effect=timeout_before_run_child
            ), mock.patch.object(
                server.asyncio, "create_subprocess_exec"
            ) as spawn:
                report = await service.validate("config.pbtxt")
            self.assertEqual(report["status_code"], "DEADLINE_EXCEEDED")
            self.assertFalse(spawn.called)
            self.assertEqual(len(duplicates), 1)
            with self.assertRaises(OSError):
                os.fstat(duplicates[0])

    async def test_late_precreation_spawn_retains_duplicate_after_close(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "root"
            root.mkdir()
            (root / "config.pbtxt").write_text("original")
            observed = Path(directory) / "observed"
            pidfile = Path(directory) / "pid"
            helper = fake_helper(
                directory,
                "import os, sys, time\n"
                "fd = int(sys.argv[sys.argv.index('--config-root-fd') + 1])\n"
                "child = os.open('config.pbtxt', os.O_RDONLY, dir_fd=fd)\n"
                f"open({str(observed)!r}, 'wb').write(os.read(child, 100))\n"
                f"open({str(pidfile)!r}, 'w').write(str(os.getpid()))\n"
                "os.close(child)\n"
                "time.sleep(30)\n",
            )
            service = server.ValidationService(str(root), str(helper), timeout=0.05)
            self.addAsyncCleanup(service.close)
            entered, release = asyncio.Event(), asyncio.Event()
            original_spawn = asyncio.create_subprocess_exec
            captured = []

            async def delayed_before_actual_spawn(*args, **kwargs):
                captured.append(kwargs["pass_fds"][0])
                entered.set()
                await release.wait()
                child = await original_spawn(*args, **kwargs)
                for _ in range(200):
                    if observed.exists():
                        break
                    await asyncio.sleep(0.005)
                return child

            with mock.patch.object(
                server.asyncio, "create_subprocess_exec", delayed_before_actual_spawn
            ):
                pending = asyncio.create_task(service.validate("config.pbtxt"))
                try:
                    await entered.wait()
                    original = Path(directory) / "original"
                    root.rename(original)
                    root.mkdir()
                    (root / "config.pbtxt").write_text("replacement")
                    report = await pending
                    self.assertEqual(report["status_code"], "UNAVAILABLE")
                    self.assertIn("restart", report["message"].lower())
                    await service.close()
                    self.assertEqual(
                        (os.fstat(captured[0]).st_dev, os.fstat(captured[0]).st_ino),
                        (original.stat().st_dev, original.stat().st_ino),
                    )
                    refused = await service.validate("other.pbtxt")
                    self.assertEqual(refused["status_code"], "UNAVAILABLE")
                    self.assertIn("restart", refused["message"].lower())
                finally:
                    release.set()
                for _ in range(300):
                    if observed.exists() and pidfile.exists():
                        break
                    await asyncio.sleep(0.01)
                self.assertEqual(observed.read_bytes(), b"original")
                for _ in range(300):
                    try:
                        os.fstat(captured[0])
                    except OSError:
                        break
                    await asyncio.sleep(0.01)
                else:
                    self.fail("spawn-owned root duplicate was not closed")
                for _ in range(300):
                    try:
                        os.kill(int(pidfile.read_text()), 0)
                    except ProcessLookupError:
                        break
                    await asyncio.sleep(0.01)
                else:
                    self.fail("late child was not reaped")

    async def test_input_internal_report_over_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.pbtxt"
            path.write_text("")
            helper = fake_helper(
                directory,
                "import json, sys\n"
                "print(json.dumps(dict(schema_version=1, scope='config_integrity', "
                "config_path=sys.argv[-1], stage='input', result='not_run', "
                "status_code='INTERNAL', message='read failed', limitations="
                + repr(LIMITATIONS)
                + ")))\n"
                "sys.exit(3)\n",
            )
            params = StdioServerParameters(
                command=str(SERVER),
                args=["--validator", str(helper), "--config-root", str(path.parent)],
            )
            async with stdio_client(params) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    await client.initialize()
                    result = await client.call_tool(
                        "validate_config", {"config_path": "config.pbtxt"}
                    )
                    self.assertTrue(result.isError)
                    self.assertEqual(
                        (
                            result.structuredContent["stage"],
                            result.structuredContent["status_code"],
                        ),
                        ("input", "INTERNAL"),
                    )

    async def test_sdk_disconnect_reaps_active_helper(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.pbtxt"
            path.write_text("")
            pidfile = Path(directory) / "pid"
            helper = fake_helper(
                directory,
                "import os, time\n"
                f"open({str(pidfile)!r}, 'w').write(str(os.getpid()))\n"
                "time.sleep(30)\n",
            )
            params = StdioServerParameters(
                command=str(SERVER),
                args=["--validator", str(helper), "--config-root", str(path.parent)],
            )
            async with stdio_client(params) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    await client.initialize()
                    pending = asyncio.create_task(
                        client.call_tool(
                            "validate_config", {"config_path": "config.pbtxt"}
                        )
                    )
                    for _ in range(100):
                        if pidfile.exists():
                            break
                        await asyncio.sleep(0.01)
                    self.assertTrue(pidfile.exists())
                    pending.cancel()
                    with self.assertRaises(asyncio.CancelledError):
                        await pending
            pid = int(pidfile.read_text())
            for _ in range(100):
                try:
                    os.kill(pid, 0)
                except ProcessLookupError:
                    break
                await asyncio.sleep(0.01)
            else:
                self.fail("helper survived MCP client disconnect")

    async def test_overflow_and_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.pbtxt"
            path.write_text("")
            overflow = fake_helper(directory, "print('x' * 70000)\n")
            overflow_service = server.ValidationService(str(path.parent), str(overflow))
            self.addAsyncCleanup(overflow_service.close)
            report = await overflow_service.validate("config.pbtxt")
            self.assertEqual(
                (report["stage"], report["status_code"]),
                ("adapter", "RESOURCE_EXHAUSTED"),
            )
            mismatch = fake_helper(directory, "print('{}')\n")
            mismatch_service = server.ValidationService(str(path.parent), str(mismatch))
            self.addAsyncCleanup(mismatch_service.close)
            report = await mismatch_service.validate("config.pbtxt")
            self.assertEqual(
                (report["stage"], report["status_code"]), ("adapter", "DATA_LOSS")
            )

    async def test_close_reaps_active_child(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.pbtxt"
            path.write_text("")
            pidfile = Path(directory) / "pid"
            helper = fake_helper(
                directory,
                "import os, time\n"
                f"open({str(pidfile)!r}, 'w').write(str(os.getpid()))\n"
                "time.sleep(30)\n",
            )
            service = server.ValidationService(str(path.parent), str(helper))
            self.addAsyncCleanup(service.close)
            task = asyncio.create_task(service.validate("config.pbtxt"))
            for _ in range(100):
                if pidfile.exists():
                    break
                await asyncio.sleep(0.01)
            self.assertTrue(pidfile.exists())
            await service.close()
            with self.assertRaises(asyncio.CancelledError):
                await task
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pidfile.read_text()), 0)

    async def test_timeout_busy_and_cancel_during_spawn_reap(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.pbtxt"
            path.write_text("")
            pidfile = Path(directory) / "pid"
            helper = fake_helper(
                directory,
                "import os, time\n"
                f"open({str(pidfile)!r}, 'w').write(str(os.getpid()))\n"
                "time.sleep(30)\n",
            )
            service = server.ValidationService(
                str(path.parent), str(helper), timeout=0.2
            )
            self.addAsyncCleanup(service.close)
            first = asyncio.create_task(service.validate("config.pbtxt"))
            for _ in range(100):
                if pidfile.exists():
                    break
                await asyncio.sleep(0.01)
            self.assertTrue(pidfile.exists())
            busy = await service.validate("other.pbtxt")
            self.assertEqual(
                (busy["status_code"], busy["config_path"]),
                ("RESOURCE_EXHAUSTED", "other.pbtxt"),
            )
            timed_out = await first
            self.assertEqual(
                (timed_out["status_code"], timed_out["config_path"]),
                ("DEADLINE_EXCEEDED", "config.pbtxt"),
            )
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pidfile.read_text()), 0)
            pidfile.unlink()
            service = server.ValidationService(str(path.parent), str(helper))
            self.addAsyncCleanup(service.close)
            entered, release = asyncio.Event(), asyncio.Event()
            original = asyncio.create_subprocess_exec

            async def delayed_spawn(*args, **kwargs):
                child = await original(*args, **kwargs)
                for _ in range(100):
                    if pidfile.exists():
                        break
                    await asyncio.sleep(0.01)
                entered.set()
                await release.wait()
                return child

            with mock.patch.object(
                server.asyncio, "create_subprocess_exec", delayed_spawn
            ):
                task = asyncio.create_task(service.validate("config.pbtxt"))
                await entered.wait()
                task.cancel()
                release.set()
                with self.assertRaises(asyncio.CancelledError):
                    await task
            self.assertTrue(pidfile.exists())
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pidfile.read_text()), 0)
            pidfile.unlink()

            service = server.ValidationService(
                str(path.parent), str(helper), timeout=0.1
            )
            self.addAsyncCleanup(service.close)
            entered, release = asyncio.Event(), asyncio.Event()

            async def slow_spawn(*args, **kwargs):
                child = await original(*args, **kwargs)
                entered.set()
                await release.wait()
                return child

            with mock.patch.object(
                server.asyncio, "create_subprocess_exec", slow_spawn
            ):
                task = asyncio.create_task(service.validate("config.pbtxt"))
                await entered.wait()
                await asyncio.sleep(0.15)
                release.set()
                report = await task
            self.assertEqual(report["status_code"], "DEADLINE_EXCEEDED")
            self.assertTrue(pidfile.exists())
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pidfile.read_text()), 0)


if __name__ == "__main__":
    unittest.main()
