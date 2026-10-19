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
    async def test_real_sdk_stdio_exchange(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.pbtxt"
            path.write_text("")
            direct = await server.ValidationService(
                str(path), str(VALIDATOR)
            ).validate()
            self.assertEqual(direct["result"], "valid", direct)
            # The SDK defaults to a restricted environment. Keep the container's
            # ROS library paths when it starts the Bazel server from this test.
            params = StdioServerParameters(
                command=str(SERVER),
                args=["--validator", str(VALIDATOR), "--config", str(path)],
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
                            "properties": {},
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
                    valid = await client.call_tool("validate_config", {})
                    self.assertFalse(valid.isError, valid.model_dump_json())
                    self.assertEqual(valid.structuredContent["result"], "valid")
                    self.assertEqual(
                        json.loads(valid.content[0].text), valid.structuredContent
                    )
                    path.write_text("unknown_field: 1")
                    invalid = await client.call_tool("validate_config", {})
                    self.assertFalse(invalid.isError)
                    self.assertEqual(invalid.structuredContent["result"], "invalid")
                    path.unlink()
                    missing = await client.call_tool("validate_config", {})
                    self.assertTrue(missing.isError)
                    self.assertEqual(missing.structuredContent["stage"], "input")
                    self.assertTrue(
                        (
                            await client.call_tool(
                                "validate_config", {"path": str(path)}
                            )
                        ).isError
                    )
                    self.assertTrue((await client.call_tool("unknown", {})).isError)

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
                args=["--validator", str(helper), "--config", str(path)],
            )
            async with stdio_client(params) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    await client.initialize()
                    result = await client.call_tool("validate_config", {})
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
                args=["--validator", str(helper), "--config", str(path)],
            )
            async with stdio_client(params) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    await client.initialize()
                    pending = asyncio.create_task(
                        client.call_tool("validate_config", {})
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
            report = await server.ValidationService(str(path), str(overflow)).validate()
            self.assertEqual(
                (report["stage"], report["status_code"]),
                ("adapter", "RESOURCE_EXHAUSTED"),
            )
            mismatch = fake_helper(directory, "print('{}')\n")
            report = await server.ValidationService(str(path), str(mismatch)).validate()
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
            service = server.ValidationService(str(path), str(helper))
            task = asyncio.create_task(service.validate())
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
            service = server.ValidationService(str(path), str(helper), timeout=0.2)
            first = asyncio.create_task(service.validate())
            for _ in range(100):
                if pidfile.exists():
                    break
                await asyncio.sleep(0.01)
            self.assertTrue(pidfile.exists())
            self.assertEqual(
                (await service.validate())["status_code"], "RESOURCE_EXHAUSTED"
            )
            self.assertEqual((await first)["status_code"], "DEADLINE_EXCEEDED")
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pidfile.read_text()), 0)
            pidfile.unlink()
            service = server.ValidationService(str(path), str(helper))
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
                task = asyncio.create_task(service.validate())
                await entered.wait()
                task.cancel()
                release.set()
                with self.assertRaises(asyncio.CancelledError):
                    await task
            self.assertTrue(pidfile.exists())
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pidfile.read_text()), 0)
            pidfile.unlink()

            service = server.ValidationService(str(path), str(helper), timeout=0.1)
            entered, release = asyncio.Event(), asyncio.Event()

            async def slow_spawn(*args, **kwargs):
                child = await original(*args, **kwargs)
                entered.set()
                await release.wait()
                return child

            with mock.patch.object(
                server.asyncio, "create_subprocess_exec", slow_spawn
            ):
                task = asyncio.create_task(service.validate())
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
