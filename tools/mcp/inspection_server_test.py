"""Seven-tool inspection contract, including config-free discovery."""

import asyncio
import copy
import importlib.util
import json
import os
import tempfile
import time
import unittest
from pathlib import Path

from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client

from tools.mcp.config_validation_server import ValidationService
from tools.mcp.inspection_adapters import CppSetupInspector
from tools.mcp.snapshot_observer import SnapshotObserver

RUNFILES = Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"]
SERVER = RUNFILES / "tools/mcp/config_validation_server"
VALIDATOR = RUNFILES / "config/joshua_validate_config"
INSPECTOR = RUNFILES / "config/joshua_inspect_config"
FIXTURE = RUNFILES / "node_generator/observation_fixture"


def candidate(name="camera", node_id=2):
    return (
        f'general {{name: "{name}"}} robot {{perceptions {{single_perceptions {{'
        f'sensor_name: "{name}" sensor_type: IMAGE opencv_config {{}} '
        f"node {{id: {node_id} node_type: CAMERA_PUBLISHER}} }} }} }}"
    )


def oversized_candidate():
    entries = []
    for i in range(128):
        name = str(i) + "x" * 390
        entries.append(
            f'single_perceptions {{sensor_name: "{name}" sensor_type: IMAGE '
            "opencv_config {} node {id: 2 node_type: CAMERA_PUBLISHER}}"
        )
    return "robot {perceptions {" + " ".join(entries) + "}}"


class InspectionContractTest(unittest.TestCase):
    def test_seven_async_methods_have_small_backend_interfaces(self):
        spec = importlib.util.find_spec("tools.mcp.interfaces")
        self.assertIsNotNone(
            spec, "Seven-tool SetupInspector/RuntimeObserver API is missing"
        )
        import inspect

        from tools.mcp.interfaces import RuntimeObserver, SetupInspector

        for cls, names in (
            (
                SetupInspector,
                (
                    "list_supported_components",
                    "validate_config",
                    "list_devices",
                    "describe_device",
                ),
            ),
            (
                RuntimeObserver,
                ("get_runtime_status", "get_recent_logs", "get_feedback"),
            ),
        ):
            self.assertEqual(set(cls.__abstractmethods__), set(names))
            for name in names:
                self.assertTrue(inspect.iscoroutinefunction(getattr(cls, name)))
        self.assertEqual(
            list(
                inspect.signature(SetupInspector.list_supported_components).parameters
            ),
            ["self"],
        )


class InspectionBehaviorTest(unittest.IsolatedAsyncioTestCase):
    async def test_oversized_helper_report_is_one_bounded_error(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "large.pbtxt").write_text(oversized_candidate())
            root_fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
            try:
                process = await asyncio.create_subprocess_exec(
                    str(INSPECTOR),
                    "--operation",
                    "list_devices",
                    "--config-root-fd",
                    str(root_fd),
                    "--config",
                    "large.pbtxt",
                    pass_fds=(root_fd,),
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE,
                )
            finally:
                os.close(root_fd)
            try:
                stdout, stderr = await asyncio.wait_for(process.communicate(), 10)
            finally:
                if process.returncode is None:
                    process.kill()
                    await process.wait()
            self.assertEqual(process.returncode, 1, stderr)
            self.assertLessEqual(len(stdout), 65536)
            self.assertTrue(stdout.endswith(b"\n"))
            self.assertEqual(stdout.count(b"\n"), 1)
            report = json.loads(stdout)
            self.assertEqual(report["scope"], "setup_inspection")
            self.assertEqual(report["stage"], "inspection")
            self.assertEqual(report["status_code"], "RESOURCE_EXHAUSTED")
            self.assertEqual(report["data"], {})
            print(f"Bounded oversized helper report: {len(stdout)} bytes")

    async def test_oversized_sdk_report_preserves_helper_error_and_recovers(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "large.pbtxt").write_text(oversized_candidate())
            Path(directory, "small.pbtxt").write_text(candidate("small"))
            params = StdioServerParameters(
                command=str(SERVER),
                args=[
                    "--validator",
                    str(VALIDATOR),
                    "--inspector",
                    str(INSPECTOR),
                    "--config-root",
                    directory,
                ],
                env={"LD_LIBRARY_PATH": os.environ.get("LD_LIBRARY_PATH", "")},
            )
            async with stdio_client(params) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    await client.initialize()
                    result = await client.call_tool(
                        "list_devices", {"config_path": "large.pbtxt"}
                    )
                    self.assertTrue(result.isError, result)
                    report = result.structuredContent
                    self.assertLessEqual(len(json.dumps(report).encode("utf-8")), 65536)
                    self.assertEqual(report["scope"], "setup_inspection")
                    self.assertEqual(report["operation"], "list_devices")
                    self.assertEqual(report["stage"], "inspection")
                    self.assertEqual(report["status_code"], "RESOURCE_EXHAUSTED")
                    self.assertEqual(report["data"], {})
                    result = await client.call_tool(
                        "list_devices", {"config_path": "small.pbtxt"}
                    )
                    self.assertFalse(result.isError, result)
                    self.assertEqual(
                        result.structuredContent["data"]["devices"][0]["device_id"],
                        "sensor:small",
                    )

    async def test_non_ascii_mcp_report_is_bounded_and_recovers(self):
        with tempfile.TemporaryDirectory() as directory:
            text = "\u6f22" * 12000
            large = candidate().replace('name: "camera"', f'name: "{text}"', 1)
            Path(directory, "large.pbtxt").write_text(large, encoding="utf-8")
            Path(directory, "small.pbtxt").write_text(candidate("small"))
            backend = CppSetupInspector(
                ValidationService(directory, str(VALIDATOR)), str(INSPECTOR)
            )
            self.addAsyncCleanup(backend.aclose)
            report = await backend.list_devices("large.pbtxt")
            self.assertEqual(report["status_code"], "OK", report)
            self.assertLess(len(json.dumps(report, ensure_ascii=False).encode()), 65536)
            self.assertGreater(len(json.dumps(report, ensure_ascii=True)), 65536)
            params = StdioServerParameters(
                command=str(SERVER),
                args=[
                    "--validator",
                    str(VALIDATOR),
                    "--inspector",
                    str(INSPECTOR),
                    "--config-root",
                    directory,
                ],
                env={"LD_LIBRARY_PATH": os.environ.get("LD_LIBRARY_PATH", "")},
            )
            async with stdio_client(params) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    await client.initialize()
                    result = await client.call_tool(
                        "list_devices", {"config_path": "large.pbtxt"}
                    )
                    self.assertTrue(result.isError)
                    self.assertEqual(
                        result.structuredContent["status_code"], "RESOURCE_EXHAUSTED"
                    )
                    self.assertEqual(result.structuredContent["data"], {})
                    self.assertLessEqual(len(result.content[0].text.encode()), 65536)
                    result = await client.call_tool(
                        "list_devices", {"config_path": "small.pbtxt"}
                    )
                    self.assertFalse(result.isError, result)
                    self.assertEqual(
                        result.structuredContent["data"]["devices"][0]["device_id"],
                        "sensor:small",
                    )

    async def test_catalog_no_config_then_multiple_candidates_and_confined_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            args = [
                "--validator",
                str(VALIDATOR),
                "--inspector",
                str(INSPECTOR),
                "--config-root",
                directory,
            ]
            async with stdio_client(
                StdioServerParameters(
                    command=str(SERVER),
                    args=args,
                    # The Docker entrypoint sources ROS; the SDK otherwise strips
                    # its shared-library path from the server/helper environment.
                    env={"LD_LIBRARY_PATH": os.environ.get("LD_LIBRARY_PATH", "")},
                )
            ) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    await client.initialize()
                    tools = (await client.list_tools()).tools
                    self.assertEqual(
                        {t.name for t in tools},
                        {
                            "list_supported_components",
                            "validate_config",
                            "list_devices",
                            "describe_device",
                            "get_runtime_status",
                            "get_recent_logs",
                            "get_feedback",
                        },
                    )
                    self.assertTrue(all(t.annotations.readOnlyHint for t in tools))
                    self.assertTrue(all(t.outputSchema for t in tools))
                    result = await client.call_tool("list_supported_components", {})
                    self.assertFalse(result.isError, result)
                    catalog = result.structuredContent["data"]
                    scopes = {x["name"]: x["support_scope"] for x in catalog["boards"]}
                    self.assertEqual(scopes["MOCK"], "test_only")
                    self.assertEqual(scopes["HOST_GPIO"], "unimplemented")
                    serial = next(
                        x
                        for x in catalog["communication"]
                        if x["name"] == "SERIAL" and x["transport"] == "MESSAGE"
                    )
                    self.assertIn("CALLER_SIZED", serial["configuration_constraints"])
                    self.assertIn("JOSHUA_WIRE", serial["configuration_constraints"])
                    self.assertNotIn(
                        "MESSAGE_AND_CYCLIC",
                        {x["transport"] for x in catalog["communication"]},
                    )
                    self.assertEqual(
                        next(
                            x for x in catalog["motors"] if x["name"] == "MOTOR_TI_DEMO"
                        )["support_scope"],
                        "demo_only",
                    )
                    audio = next(x for x in catalog["sensors"] if x["name"] == "AUDIO")
                    self.assertEqual(audio["sensor_config"], "alsa_audio_config")
                    self.assertEqual(audio["support_scope"], "installed")
                    for name in ("camera_a", "camera_b"):
                        Path(directory, name + ".pbtxt").write_text(candidate(name))
                        result = await client.call_tool(
                            "list_devices", {"config_path": name + ".pbtxt"}
                        )
                        self.assertFalse(result.isError, result)
                        device = result.structuredContent["data"]["devices"][0]
                        self.assertEqual(device["device_id"], "sensor:" + name)
                        self.assertEqual(device["hardware_readiness"], "unknown")
                        result = await client.call_tool(
                            "describe_device",
                            {
                                "config_path": name + ".pbtxt",
                                "device_id": "sensor:" + name,
                            },
                        )
                        self.assertFalse(result.isError, result)
                    Path(directory, "link.pbtxt").symlink_to("camera_a.pbtxt")
                    for name, code in (
                        ("link.pbtxt", "INVALID_ARGUMENT"),
                        ("missing.pbtxt", "NOT_FOUND"),
                        ("../outside.pbtxt", "INVALID_ARGUMENT"),
                    ):
                        result = await client.call_tool(
                            "list_devices", {"config_path": name}
                        )
                        self.assertTrue(result.isError)
                        self.assertEqual(
                            result.structuredContent["status_code"], code, result
                        )
                    Path(directory, "bad.pbtxt").write_text("general { name: ")
                    result = await client.call_tool(
                        "list_devices", {"config_path": "bad.pbtxt"}
                    )
                    self.assertEqual(result.structuredContent["stage"], "load")
                    self.assertRegex(result.structuredContent["message"], r"[Ll]ine")
                    for name, args in (
                        ("get_recent_logs", {"min_level": []}),
                        ("get_feedback", {"sensor_id": []}),
                    ):
                        result = await client.call_tool(name, args)
                        self.assertTrue(result.isError)

    async def test_describe_complete_config_detects_duplicates_beyond_inventory_cap(
        self,
    ):
        with tempfile.TemporaryDirectory() as directory:
            entries = []
            for i in range(131):
                name = "duplicate" if i in (0, 130) else f"camera_{i}"
                entries.append(
                    f'single_perceptions {{sensor_name: "{name}" sensor_type: IMAGE '
                    "opencv_config {} node {id: 2 node_type: CAMERA_PUBLISHER}}"
                )
            Path(directory, "large.pbtxt").write_text(
                "robot {perceptions {" + " ".join(entries) + "}}"
            )
            service = ValidationService(directory, str(VALIDATOR))
            backend = CppSetupInspector(service, str(INSPECTOR))
            self.addAsyncCleanup(backend.aclose)
            result = await backend.list_devices("large.pbtxt")
            self.assertEqual(len(result["data"]["devices"]), 128, result)
            result = await backend.describe_device("large.pbtxt", "sensor:duplicate")
            self.assertEqual(result["status_code"], "FAILED_PRECONDITION", result)
            result = await backend.describe_device("large.pbtxt", "sensor:camera_129")
            self.assertEqual(result["status_code"], "OK", result)
            Path(directory, "long.pbtxt").write_text(candidate("c" * 512))
            result = await backend.list_devices("long.pbtxt")
            self.assertEqual(result["data"]["devices"], [])
            self.assertEqual(result["data"]["omitted_devices"], 1)


class SnapshotBehaviorTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.reader = SnapshotObserver(self.directory.name)
        self.addAsyncCleanup(self.reader.aclose)
        self.fixture = await asyncio.create_subprocess_exec(
            str(FIXTURE),
            self.directory.name,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=None,
            env={**os.environ, "ROS_LOCALHOST_ONLY": "1", "ROS_DOMAIN_ID": "143"},
        )
        self.addAsyncCleanup(self.stop_fixture)
        await self.until(lambda r: r["data"].get("state") == "available")

    async def stop_fixture(self):
        if self.fixture.returncode is None:
            self.fixture.stdin.write(b"exit\n")
            await self.fixture.stdin.drain()
            try:
                await asyncio.wait_for(self.fixture.wait(), 3)
            except asyncio.TimeoutError:
                self.fixture.kill()
                await self.fixture.wait()

    async def mode(self, mode):
        self.fixture.stdin.write(mode.encode() + b"\n")
        await self.fixture.stdin.drain()

    async def until(self, predicate, timeout=8, read=None):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            result = await (read() if read else self.reader.get_feedback("encoder"))
            if predicate(result):
                return result
            await asyncio.sleep(0.05)
        self.fail(
            f"Observation condition timed out: status={result.get('status_code')}, "
            f"availability={result.get('availability')}, "
            f"state={result.get('data', {}).get('state')}; "
            f"fixture exit {self.fixture.returncode}"
        )

    async def test_real_ros_observer_snapshot_sdk_and_source_rejection(self):
        args = [
            "--validator",
            str(VALIDATOR),
            "--inspector",
            str(INSPECTOR),
            "--config-root",
            self.directory.name,
            "--observation-dir",
            self.directory.name,
        ]
        async with stdio_client(
            StdioServerParameters(
                command=str(SERVER),
                args=args,
                # The Docker entrypoint sources ROS; the SDK otherwise strips
                # its shared-library path from the server/helper environment.
                env={"LD_LIBRARY_PATH": os.environ.get("LD_LIBRARY_PATH", "")},
            )
        ) as (reader, writer):
            async with ClientSession(reader, writer) as client:
                await client.initialize()
                result = await client.call_tool(
                    "get_feedback", {"sensor_id": "encoder"}
                )
                self.assertFalse(result.isError, result)
                sample = result.structuredContent["data"]["sample"]
                self.assertEqual(sample["position"], 0.75)
                self.assertEqual(sample["units"], "radians")
                self.assertIn(len(sample["publisher_gid"]), (32, 48))
                self.assertEqual(sample["acquisition_freshness"], "unknown")
                result = await client.call_tool("get_runtime_status", {})
                self.assertEqual(
                    result.structuredContent["data"]["config"]["name"],
                    "observation-fixture",
                )
                await asyncio.sleep(0.2)
                result = await client.call_tool("get_recent_logs", {})
                self.assertTrue(result.structuredContent["data"]["entries"], result)
                self.assertEqual(
                    result.structuredContent["data"]["entries"][0][
                        "runtime_association"
                    ],
                    "runtime_association_unverified",
                )
                self.assertIn(
                    "한글", result.structuredContent["data"]["entries"][0]["message"]
                )
                result = await client.call_tool("get_recent_logs", {"min_level": []})
                self.assertEqual(
                    result.structuredContent["status_code"], "INVALID_ARGUMENT"
                )
        old = (await self.reader.get_feedback("encoder"))["data"]["sample"][
            "publisher_gid"
        ]
        await self.mode("restart_silent")
        await self.until(
            lambda r: r["data"].get("state") == "unavailable"
            and r["data"].get("sample") is None
        )
        await self.mode("normal")
        changed = await self.until(lambda r: r["data"].get("state") == "available")
        self.assertNotEqual(changed["data"]["sample"]["publisher_gid"], old)
        await self.mode("invalid")
        await self.until(lambda r: r["data"].get("state") == "invalid")
        await self.mode("ambiguous")
        await self.until(
            lambda r: r["data"].get("state") in {"ambiguous", "invalid"}
            and r["data"].get("sample") is None
        )
        await self.mode("wrong")
        await self.until(lambda r: r["data"].get("state") == "wrong_publisher")
        await self.mode("normal")
        await self.until(lambda r: r["data"].get("state") == "available")
        await self.mode("missing")
        await self.until(
            lambda r: r["data"].get("state") == "missing_publisher"
            and r["data"].get("sample") is None
        )

    async def test_gid_storage_sizes_are_bounded(self):
        await self.stop_fixture()
        path = Path(self.directory.name, "snapshot.json")
        original = json.loads(path.read_text())
        observed = original["feedback"]["sample"]["publisher_gid"]
        self.assertIn(len(observed), (32, 48))
        print(f"Observed ROS publisher GID: {len(observed) // 2} bytes")
        for gid in (observed[:32], observed.ljust(48, "0")):
            with self.subTest(valid_gid_length=len(gid)):
                snapshot = copy.deepcopy(original)
                snapshot["feedback"]["sample"]["publisher_gid"] = gid
                path.write_text(json.dumps(snapshot))
                result = await self.reader.get_feedback("encoder")
                self.assertEqual(result["status_code"], "OK", result)
                self.assertEqual(result["data"]["sample"]["publisher_gid"], gid)
        for gid in (
            "",
            "a" * 30,
            "a" * 34,
            "a" * 46,
            "a" * 50,
            "A" * 32,
            "g" * 48,
            None,
            [],
        ):
            with self.subTest(invalid_gid=gid):
                snapshot = copy.deepcopy(original)
                snapshot["feedback"]["sample"]["publisher_gid"] = gid
                path.write_text(json.dumps(snapshot))
                self.assertEqual(
                    (await self.reader.get_feedback("encoder"))["status_code"],
                    "DATA_LOSS",
                )

    async def test_untrusted_log_fields_preserve_status(self):
        for mode in ("bad_log", "bad_level"):
            before = await self.reader.get_runtime_status()
            dropped = (await self.reader.get_recent_logs())["data"]["dropped"]
            await self.mode(mode)
            await self.until(
                lambda r: r["status_code"] == "OK" and r["data"]["dropped"] > dropped,
                read=self.reader.get_recent_logs,
            )
            current = await self.reader.get_runtime_status()
            self.assertEqual(current["status_code"], "OK")
            self.assertGreater(
                current["snapshot_sequence"], before["snapshot_sequence"]
            )
            self.assertEqual(
                (await self.reader.get_feedback("encoder"))["data"]["state"],
                "available",
            )

    async def test_escaped_size_preserves_status(self):
        await self.mode("escaped_burst")
        # Wait for a full retained ring, including JSON escaping and process facts.
        await self.until(
            lambda r: r["data"].get("cursor", {}).get("sequence", 0) > 64,
            read=self.reader.get_recent_logs,
        )
        current = await self.reader.get_runtime_status()
        self.assertEqual(current["availability"], "fresh")
        self.assertLessEqual(
            Path(self.directory.name, "snapshot.json").stat().st_size, 65536
        )

    async def test_oversized_position_integer_returns_data_loss_for_each_tool(self):
        await self.stop_fixture()
        path = Path(self.directory.name, "snapshot.json")
        original = json.loads(path.read_text())
        for position in (10**400, -(10**400)):
            bad = copy.deepcopy(original)
            bad["feedback"]["sample"]["position"] = position
            path.write_text(json.dumps(bad))
            for operation, arguments in (
                (self.reader.get_runtime_status, ()),
                (self.reader.get_recent_logs, ()),
                (self.reader.get_feedback, ("encoder",)),
            ):
                with self.subTest(position_sign=position > 0, tool=operation.__name__):
                    report = await operation(*arguments)
                    self.assertEqual(report["status_code"], "DATA_LOSS")
                    self.assertEqual(report["availability"], "unavailable")
        path.write_text(json.dumps(original))
        self.assertEqual((await self.reader.get_runtime_status())["status_code"], "OK")

    async def test_snapshot_rejection_freshness_cursor_and_directory_pinning(self):
        await self.mode("burst")
        await self.until(
            lambda r: r["data"].get("retention_gap", 0) > 0,
            read=self.reader.get_recent_logs,
        )
        await self.stop_fixture()
        path = Path(self.directory.name, "snapshot.json")
        original = json.loads(path.read_text())
        self.assertLessEqual(path.stat().st_size, 65536)
        for key, val in (
            ("boot_id", "other-boot"),
            ("time_namespace", "time:[different]"),
            (
                "captured_boottime_ns",
                str(time.clock_gettime_ns(time.CLOCK_BOOTTIME) + 10**12),
            ),
            ("phase", "ready"),
            ("feedback", []),
            ("processes", {}),
        ):
            bad = copy.deepcopy(original)
            bad[key] = val
            path.write_text(json.dumps(bad))
            self.assertEqual(
                (await self.reader.get_runtime_status())["status_code"],
                "DATA_LOSS",
                key,
            )
        path.write_text(json.dumps(original))
        logs = await self.reader.get_recent_logs(limit=1)
        self.assertEqual(len(logs["data"]["entries"]), 1)
        self.assertGreater(logs["data"]["retention_gap"], 0)
        first = logs["data"]["entries"][0]["sequence"]
        second = await self.reader.get_recent_logs(logs["data"]["cursor"], limit=1)
        self.assertEqual(second["data"]["entries"][0]["sequence"], first + 1)
        filtered = await self.reader.get_recent_logs(min_level="FATAL")
        self.assertEqual(
            filtered["data"]["cursor"]["sequence"], original["logs"]["latest_sequence"]
        )
        future = await self.reader.get_recent_logs(
            {
                "instance_id": original["instance_id"],
                "sequence": original["logs"]["latest_sequence"] + 1,
            }
        )
        self.assertEqual(future["status_code"], "INVALID_ARGUMENT")
        reset = await self.reader.get_recent_logs(
            {"instance_id": "previous-run", "sequence": 999999}
        )
        self.assertTrue(reset["data"]["cursor_reset"])
        stale = copy.deepcopy(original)
        stale["captured_boottime_ns"] = str(
            time.clock_gettime_ns(time.CLOCK_BOOTTIME) - 4_000_000_000
        )
        # Source receipt times must remain internally ordered.
        for entry in stale["logs"]["entries"]:
            entry["receipt_boottime_ns"] = stale["captured_boottime_ns"]
        stale["feedback"]["sample"]["receipt_boottime_ns"] = stale[
            "captured_boottime_ns"
        ]
        path.write_text(json.dumps(stale))
        self.assertEqual(
            (await self.reader.get_runtime_status())["availability"], "stale"
        )
        self.assertEqual(
            (await self.reader.get_feedback("encoder"))["data"]["state"], "stale"
        )
        malformed = [
            ("observation", "state", "ready"),
            ("observation", "error", []),
            ("feedback", "state", "ready"),
            ("feedback", "source", []),
            ("logs", "entries", {}),
            ("logs", "latest_sequence", []),
            ("config", "source", {}),
        ]
        for outer, key, value in malformed:
            bad = copy.deepcopy(original)
            bad[outer][key] = value
            path.write_text(json.dumps(bad))
            self.assertEqual(
                (await self.reader.get_runtime_status())["status_code"],
                "DATA_LOSS",
                (outer, key),
            )
        bad = copy.deepcopy(original)
        bad["feedback"]["sample"]["position"] = []
        path.write_text(json.dumps(bad))
        self.assertEqual(
            (await self.reader.get_runtime_status())["status_code"], "DATA_LOSS"
        )
        path.write_text(json.dumps(original))
        path.chmod(0o644)
        self.assertEqual(
            (await self.reader.get_runtime_status())["status_code"], "DATA_LOSS"
        )
        path.chmod(0o600)
        link = path.with_name("hardlink")
        link.hardlink_to(path)
        self.assertEqual(
            (await self.reader.get_runtime_status())["status_code"], "DATA_LOSS"
        )
        link.unlink()
        path.write_text("x" * 65537)
        self.assertEqual(
            (await self.reader.get_runtime_status())["status_code"], "DATA_LOSS"
        )
        path.unlink()
        path.symlink_to("/etc/passwd")
        self.assertEqual(
            (await self.reader.get_runtime_status())["status_code"], "DATA_LOSS"
        )
        path.unlink()
        os.mkfifo(path)
        self.assertEqual(
            (await self.reader.get_runtime_status())["status_code"], "DATA_LOSS"
        )
        path.unlink()
        path.write_text(json.dumps(original))
        path.chmod(0o600)
        renamed = self.directory.name + "-renamed"
        os.rename(self.directory.name, renamed)
        os.mkdir(self.directory.name, 0o700)
        try:
            self.assertEqual(
                (await self.reader.get_runtime_status())["instance_id"],
                original["instance_id"],
            )
        finally:
            os.rmdir(self.directory.name)
            os.rename(renamed, self.directory.name)


class ObservationContainmentTest(unittest.IsolatedAsyncioTestCase):
    async def test_init_failure_and_output_failure_still_reap_fake_child(self):
        for fail_init in (True, False):
            with self.subTest(
                fail_init=fail_init
            ), tempfile.TemporaryDirectory() as directory:
                reader = SnapshotObserver(directory)
                try:
                    process = await asyncio.create_subprocess_exec(
                        str(FIXTURE),
                        directory,
                        *(["fail_init"] if fail_init else []),
                        stdin=asyncio.subprocess.PIPE,
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.DEVNULL,
                        env={
                            **os.environ,
                            "ROS_LOCALHOST_ONLY": "1",
                            "ROS_DOMAIN_ID": "144",
                        },
                    )
                    try:
                        if not fail_init:
                            process.stdin.write(b"block_output\n")
                            await process.stdin.drain()
                        self.assertEqual(
                            await asyncio.wait_for(process.stdout.readline(), 6),
                            b"REAPED\n",
                        )
                        self.assertIsNone(process.returncode)
                        if not fail_init:
                            self.assertTrue(Path(directory, "snapshot.json").is_dir())
                            process.stdin.write(b"restore_output\n")
                            await process.stdin.drain()
                        end = time.monotonic() + 6
                        while time.monotonic() < end:
                            result = await reader.get_runtime_status()
                            if result["data"].get("exits"):
                                break
                            await asyncio.sleep(0.05)
                        self.assertEqual(result["data"]["processes"], [])
                        self.assertEqual(result["data"]["exits"][0]["exit_code"], 0)
                        if fail_init:
                            self.assertEqual(
                                result["data"]["observation"]["state"], "disabled"
                            )
                            feedback = await reader.get_feedback("encoder")
                            self.assertEqual(feedback["data"]["state"], "unavailable")
                            self.assertIsNone(feedback["data"]["sample"])
                        process.stdin.write(b"exit\n")
                        await process.stdin.drain()
                        self.assertEqual(await asyncio.wait_for(process.wait(), 3), 0)
                    finally:
                        if process.returncode is None:
                            process.kill()
                            await process.wait()
                finally:
                    await reader.aclose()

    async def test_preflight_rejects_unsafe_directory_and_lease_before_fork(self):
        with tempfile.TemporaryDirectory() as directory:
            for kind in ("symlink", "fifo", "public", "leased"):
                lease = Path(directory, ".producer.lock")
                if kind == "symlink":
                    lease.symlink_to("/dev/null")
                elif kind == "fifo":
                    os.mkfifo(lease)
                elif kind == "public":
                    os.chmod(directory, 0o755)
                else:
                    import fcntl

                    lease.write_text("")
                    lease.chmod(0o600)
                    held = lease.open("w")
                    fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
                process = await asyncio.create_subprocess_exec(
                    str(FIXTURE),
                    directory,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE,
                )
                output, _ = await asyncio.wait_for(process.communicate(), 3)
                self.assertEqual(process.returncode, 3, kind)
                self.assertNotIn(b"REAPED", output)
                if kind == "public":
                    os.chmod(directory, 0o700)
                else:
                    if kind == "leased":
                        held.close()
                    lease.unlink()


if __name__ == "__main__":
    unittest.main()
