"""Production image source and real ROS/SDK exchange, without a capture driver."""

import asyncio
import base64
import importlib.util
import io
import json
import os
import secrets
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client
from PIL import Image

RUNFILES = Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"]
HELPER = RUNFILES / "ros2/image_observer"
FIXTURE = RUNFILES / "ros2/image_observer_fixture"
SERVER = RUNFILES / "tools/mcp/config_validation_server"
VALIDATOR = RUNFILES / "config/joshua_validate_config"
INSPECTOR = RUNFILES / "config/joshua_inspect_config"


class ImageSourceContractTest(unittest.IsolatedAsyncioTestCase):
    async def test_production_source_is_implemented(self):
        self.assertIsNotNone(
            importlib.util.find_spec("tools.mcp.image_source"),
            "production ImageSource is missing",
        )

    async def test_no_runtime_close_and_descriptor(self):
        from tools.mcp.image_source import ImageSource
        from tools.mcp.observation_source import Availability, UnavailableReason

        with tempfile.TemporaryDirectory() as directory:
            source = ImageSource(directory, str(HELPER), "sensor:webcam")
            descriptor = source.describe()
            result = await source.read_latest()
            self.assertEqual(result.availability, Availability.UNAVAILABLE)
            self.assertEqual(result.unavailable_reason, UnavailableReason.NO_DATA)
            await asyncio.gather(source.aclose(), source.aclose())
            self.assertIs(source.describe(), descriptor)
            self.assertEqual(
                (await source.read_latest()).unavailable_reason,
                UnavailableReason.CLOSED,
            )


class ImageParserTest(unittest.IsolatedAsyncioTestCase):
    async def test_rejects_malformed_manifest_without_touching_devices(self):
        from tools.mcp.image_source import ImageSource

        with tempfile.TemporaryDirectory() as directory:
            source = ImageSource(directory, str(HELPER), "sensor:webcam")
            with open("/proc/sys/kernel/random/boot_id", encoding="ascii") as file:
                boot = file.read().strip()
            base = dict(
                schema_version=1,
                instance_id="synthetic",
                camera_id="sensor:webcam",
                topic="/_private/image",
                node_name="camera_publisher_node_12",
                node_namespace="/",
                ros_type="sensor_msgs/msg/Image",
                ros_domain=210,
                boot_id=boot,
                time_namespace=os.readlink("/proc/self/ns/time"),
                clock_kind="CLOCK_BOOTTIME",
                captured_boottime_ns=str(time.clock_gettime_ns(time.CLOCK_BOOTTIME)),
                phase="monitoring",
                process_present=True,
            )
            manifest = Path(directory, "camera-source.json")
            try:
                for field, value in [
                    ("boot_id", "wrong"),
                    ("time_namespace", "wrong"),
                    ("captured_boottime_ns", "0"),
                    ("captured_boottime_ns", str(2**64 - 1)),
                    ("ros_domain", 233),
                    ("ros_domain", True),
                    ("process_present", False),
                    ("node_namespace", "/other"),
                    ("topic", "/bad//topic"),
                    ("ros_type", "sensor_msgs/msg/JointState"),
                    ("extra", 1),
                ]:
                    manifest.write_text(json.dumps(dict(base, **{field: value})))
                    manifest.chmod(0o600)
                    result = await source.read_latest()
                    self.assertEqual(
                        result.availability.value, "unavailable", (field, result)
                    )
                    self.assertIsNone(result.payload)
                manifest.unlink()
                os.mkfifo(manifest, 0o600)
                self.assertEqual(
                    (await source.read_latest()).availability.value, "unavailable"
                )
                manifest.unlink()
                manifest.symlink_to("/dev/null")
                self.assertEqual(
                    (await source.read_latest()).availability.value, "unavailable"
                )
                manifest.unlink()
                manifest.write_text(json.dumps(base))
                manifest.chmod(0o600)
                alias = Path(directory, "camera-manifest-hardlink")
                os.link(manifest, alias)
                try:
                    result = await source.read_latest()
                    self.assertEqual(result.availability.value, "unavailable")
                    self.assertIsNone(result.payload)
                    self.assertIn("Unsafe camera manifest", result.diagnostic)
                finally:
                    alias.unlink()
                manifest.unlink()
                manifest.write_text("x" * 16385)
                manifest.chmod(0o600)
                self.assertEqual(
                    (await source.read_latest()).availability.value, "unavailable"
                )
            finally:
                await source.aclose()

    async def test_typed_adapter_rejects_malformed_payload_and_scalar_metadata(self):
        from tools.mcp.image_source import ImageSource
        from tools.mcp.observation_source import UnavailableReason

        image = io.BytesIO()
        Image.new("RGB", (2, 1), "red").save(image, format="JPEG")
        jpeg = image.getvalue()
        with tempfile.TemporaryDirectory() as directory:
            for field, value in [
                ("frame_id", []),
                ("diagnostic", []),
                ("query_id", True),
                ("width", 3),
                ("source_stamp_nanosec", 1000000000),
                ("clock_id", "other"),
                ("publisher_gid", "bad"),
                ("jpeg", b"\xff\xd8\xff\xd9"),
            ]:
                helper = Path(directory, "fake")
                with open("/proc/sys/kernel/random/boot_id", encoding="ascii") as file:
                    clock = (
                        "CLOCK_BOOTTIME:"
                        + file.read().strip()
                        + ":"
                        + os.readlink("/proc/self/ns/time")
                    )
                meta = dict(
                    camera_id="sensor:webcam",
                    query_id=1,
                    availability="fresh",
                    reason="",
                    diagnostic="",
                    reset=False,
                    runtime_instance="runtime",
                    receipt_boottime_ns=str(time.clock_gettime_ns(time.CLOCK_BOOTTIME)),
                    clock_id=clock,
                    source_stamp_sec=1,
                    source_stamp_nanosec=2,
                    sample_id="helper:1",
                    width=2,
                    height=1,
                    source_width=2,
                    source_height=1,
                    source_encoding="rgb8",
                    frame_id="target",
                    publisher_gid="00" * 24,
                    topic="/camera/image",
                    node_name="camera",
                    node_namespace="/",
                    ros_domain=0,
                )
                payload = jpeg
                if field == "jpeg":
                    payload = value
                else:
                    meta[field] = value
                helper.write_text(
                    "#!/usr/bin/python3\nimport sys,struct,json,base64\n"
                    "sys.stdin.readline()\nm="
                    + repr(json.dumps(meta).encode())
                    + "\nb="
                    + repr(payload)
                    + "\nsys.stdout.buffer.write("
                    "struct.pack('!II',len(m),len(b))+m+b)\n"
                    "sys.stdout.buffer.flush()\nsys.stdin.read()\n"
                )
                helper.chmod(0o700)
                source = ImageSource(directory, str(helper), "sensor:webcam")
                try:
                    result = await source.read_latest()
                    self.assertEqual(
                        result.unavailable_reason,
                        UnavailableReason.INVALID_DATA,
                        (field, result),
                    )
                finally:
                    await source.aclose()


class ImageWireLimitTest(unittest.IsolatedAsyncioTestCase):
    async def test_final_serialized_result_limit_includes_base64(self):
        from tools.mcp.image_source import ImageFrame
        from tools.mcp.inspection_server import _camera_result
        from tools.mcp.observation_source import (
            Availability,
            ObservationResult,
            ObservationTime,
            SourceDescriptor,
        )

        class AlternateTypedSource:
            def describe(self):
                return SourceDescriptor("sensor:webcam", "image", "test")

            async def read_latest(self):
                frame = ImageFrame(
                    b"x" * (800 * 1024),
                    640,
                    480,
                    640,
                    480,
                    "rgb8",
                    "frame",
                    "00" * 24,
                    "helper:1",
                    "/camera",
                    "camera",
                    "/",
                    0,
                )
                return ObservationResult(
                    "sensor:webcam",
                    Availability.FRESH,
                    frame,
                    ObservationTime(1, "test-clock"),
                )

        result = await _camera_result(
            AlternateTypedSource(), {"camera_id": "sensor:webcam"}
        )
        self.assertTrue(result.isError)
        self.assertFalse(any(c.type == "image" for c in result.content))
        self.assertLessEqual(
            len(result.model_dump_json(by_alias=True).encode()), 1024 * 1024
        )


class ImageSdkTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = self.temp.name
        self.fixture_mode = "normal"
        self.fixture_transition = "initial normal"
        self.environment = dict(
            os.environ,
            ROS_DOMAIN_ID=str(50 + secrets.randbelow(120)),
            ROS_LOCALHOST_ONLY="1",
            RCUTILS_LOGGING_USE_STDOUT="0",
        )
        self.fixture = await asyncio.create_subprocess_exec(
            str(FIXTURE),
            self.directory,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
            env=self.environment,
        )
        await asyncio.wait_for(self.fixture.stdout.readline(), 10)

    async def asyncTearDown(self):
        if self.fixture.returncode is None:
            self.fixture.kill()
        await self.fixture.wait()
        self.temp.cleanup()

    async def mode(self, value):
        self.fixture_transition = f"{self.fixture_mode} -> {value}"
        self.fixture_mode = value
        self.fixture.stdin.write((value + "\n").encode())
        await self.fixture.stdin.drain()
        await asyncio.wait_for(self.fixture.stdout.readline(), 5)

    async def until(self, session, state, predicate=None):
        for _ in range(120):
            result = await session.call_tool(
                "get_camera_image", {"camera_id": "sensor:webcam"}
            )
            if result.structuredContent["availability"] == state and (
                predicate is None or predicate(result.structuredContent)
            ):
                return result
            await asyncio.sleep(0.05)
        self.fail(
            f"did not reach {state} in fixture transition "
            f"{self.fixture_transition}: {result}"
        )

    async def test_actual_ros_helper_adapter_sdk_jpeg_and_recovery(self):
        params = StdioServerParameters(
            command=str(SERVER),
            args=[
                "--config-root",
                self.directory,
                "--validator",
                str(VALIDATOR),
                "--inspector",
                str(INSPECTOR),
                "--observation-dir",
                self.directory,
                "--image-observer",
                str(HELPER),
                "--camera-id",
                "sensor:webcam",
            ],
            env=self.environment,
        )
        async with stdio_client(params) as (reader, writer):
            async with ClientSession(reader, writer) as session:
                await session.initialize()
                tools = (await session.list_tools()).tools
                self.assertEqual(len(tools), 10)
                image = next(t for t in tools if t.name == "get_camera_image")
                self.assertEqual(image.inputSchema["required"], ["camera_id"])
                result = await self.until(session, "fresh")
                blocks = [c for c in result.content if c.type == "image"]
                self.assertEqual(len(blocks), 1)
                self.assertEqual(blocks[0].mimeType, "image/jpeg")
                self.assertTrue(result.structuredContent["topic"].startswith("/_"))
                gid_bytes = len(result.structuredContent["publisher_gid"]) // 2
                self.assertEqual(gid_bytes, 16 if sys.version_info.minor == 12 else 24)
                print("Real image publisher GID bytes:", gid_bytes)
                decoded = Image.open(
                    io.BytesIO(base64.b64decode(blocks[0].data, validate=True))
                )
                self.assertEqual(decoded.size, (640, 360))
                red, green, blue = decoded.getpixel((100, 100))
                self.assertGreater(red, 230)
                self.assertLess(green, 20)
                self.assertLess(blue, 20)
                self.assertEqual(
                    result.structuredContent["source_timestamp_semantics"],
                    "publication_time",
                )
                self.assertEqual(
                    result.structuredContent["acquisition_freshness"], "unknown"
                )
                self.assertLessEqual(
                    len(result.model_dump_json(by_alias=True).encode()), 1024 * 1024
                )
                bad = await session.call_tool(
                    "get_camera_image", {"camera_id": "sensor:other"}
                )
                self.assertTrue(bad.isError)
                self.assertFalse(any(c.type == "image" for c in bad.content))
                await self.mode("silent")
                fresh = await self.until(session, "fresh")
                repeat = await self.until(session, "fresh")
                self.assertEqual(
                    repeat.structuredContent["sample_id"],
                    fresh.structuredContent["sample_id"],
                )
                self.assertEqual(
                    repeat.structuredContent["receipt_boottime_ns"],
                    fresh.structuredContent["receipt_boottime_ns"],
                )
                self.assertEqual(
                    [c.data for c in repeat.content if c.type == "image"],
                    [c.data for c in fresh.content if c.type == "image"],
                )
                await asyncio.sleep(3.2)
                stale = await self.until(session, "stale")
                self.assertFalse(any(c.type == "image" for c in stale.content))
                self.assertEqual(
                    fresh.structuredContent["sample_id"],
                    stale.structuredContent["sample_id"],
                )
                self.assertEqual(
                    fresh.structuredContent["receipt_boottime_ns"],
                    stale.structuredContent["receipt_boottime_ns"],
                )
                # Repeated writer replacement catches readers that remain matched
                # in the graph but stop delivering after a publisher disappears.
                for mode in ("wrong", "ambiguous", "missing") * 3 + (
                    "invalid",
                    "oversize",
                    "bad_stamp",
                    "bad_frame_id",
                    "bad_encoding",
                    "short",
                ):
                    await self.mode(mode)
                    result = await self.until(session, "unavailable")
                    self.assertFalse(any(c.type == "image" for c in result.content))
                    await self.mode("normal")
                    await self.until(session, "fresh")
                for mode in (
                    "padded",
                    "bgr",
                    "rebind_topic",
                    "rebind_domain",
                    "rebind_instance",
                ):
                    before = (await self.until(session, "fresh")).structuredContent
                    await self.mode(mode)
                    result = await self.until(
                        session,
                        "fresh",
                        lambda report: report.get("frame_id")
                        == "synthetic_color_target:" + mode,
                    )
                    report = result.structuredContent
                    self.assertEqual(report["source_id"], "sensor:webcam")
                    blocks = [c for c in result.content if c.type == "image"]
                    pixel = Image.open(
                        io.BytesIO(base64.b64decode(blocks[0].data))
                    ).getpixel((100, 100))
                    self.assertGreater(pixel[0], 230)
                    self.assertLess(pixel[1], 20)
                    self.assertLess(pixel[2], 20)
                    if mode.startswith("rebind"):
                        self.assertNotEqual(
                            report["runtime_instance"], before["runtime_instance"]
                        )
                        self.assertNotEqual(
                            report["sample_id"].split(":")[0],
                            before["sample_id"].split(":")[0],
                        )
                    if mode == "rebind_topic":
                        self.assertNotEqual(report["topic"], before["topic"])
                    if mode == "rebind_domain":
                        self.assertNotEqual(report["ros_domain"], before["ros_domain"])
                await self.mode("restart_silent")
                await self.until(session, "unavailable")
                await self.mode("normal")
                await self.until(session, "fresh")
        self.assertIsNone(
            self.fixture.returncode, "MCP close stopped independent publisher"
        )

    async def test_atomic_manifest_replacement_preserves_reader(self):
        from tools.mcp.image_source import ImageSource

        manifest = Path(self.directory, "camera-source.json")
        for _ in range(100):
            if manifest.exists():
                break
            await asyncio.sleep(0.01)
        self.assertTrue(
            manifest.exists(), "fixture did not publish its camera manifest"
        )
        with patch.dict(os.environ, self.environment):
            source = ImageSource(
                self.directory,
                str(RUNFILES / "ros2/image_observer_manifest_race_fixture"),
                "sensor:webcam",
            )
            try:
                for _ in range(100):
                    result = await source.read_latest()
                    # The hook replaces the named inode on every read. A benign
                    # unlink must not reset the reader before discovery completes.
                    # The producer keeps heartbeats fresh during slow discovery.
                    self.assertIsNotNone(source._process, result.diagnostic)
                    if result.availability.value == "fresh":
                        break
                    await asyncio.sleep(0.03)
                self.assertEqual(result.availability.value, "fresh", result.diagnostic)
                self.assertIsNotNone(result.payload)
                process = source._process
                for _ in range(3):
                    result = await source.read_latest()
                    self.assertEqual(
                        result.availability.value, "fresh", result.diagnostic
                    )
                    self.assertIsNotNone(result.payload)
                    self.assertIs(
                        source._process, process, "manifest rename reset helper"
                    )
            finally:
                await source.aclose()

    async def test_binding_change_inside_encoder_rejects_old_bytes(self):
        from tools.mcp.image_source import ImageSource

        for _ in range(100):
            if Path(self.directory, "camera-source.json").exists():
                break
            await asyncio.sleep(0.01)
        await self.mode("pause_manifest")
        with patch.dict(os.environ, self.environment):
            source = ImageSource(
                self.directory,
                str(RUNFILES / "ros2/image_observer_race_fixture"),
                "sensor:webcam",
            )
            try:
                for _ in range(60):
                    result = await source.read_latest()
                    if "binding changed during query" in result.diagnostic.lower():
                        break
                    self.assertIsNone(result.payload)
                    await asyncio.sleep(0.03)
                self.assertIn("binding changed during query", result.diagnostic.lower())
                self.assertIsNone(result.payload)
                self.assertIsNone(
                    source._process, "old helper not reaped after binding race"
                )
            finally:
                await source.aclose()

    async def test_codec_failure_and_oversize_clear_payload_and_reap_helper(self):
        from tools.mcp.image_source import ImageSource

        with patch.dict(os.environ, self.environment):
            for executable in (
                "image_observer_codec_failure_fixture",
                "image_observer_codec_oversize_fixture",
            ):
                source = ImageSource(
                    self.directory, str(RUNFILES / "ros2" / executable), "sensor:webcam"
                )
                try:
                    for _ in range(100):
                        result = await source.read_latest()
                        if "JPEG encoding failed" in result.diagnostic:
                            break
                        await asyncio.sleep(0.03)
                    self.assertIn("JPEG encoding failed", result.diagnostic)
                    self.assertIsNone(result.payload)
                    self.assertIsNone(source._process)
                finally:
                    await source.aclose()


if __name__ == "__main__":
    unittest.main()
