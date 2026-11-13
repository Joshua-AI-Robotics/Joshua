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
HELPER = RUNFILES / "tools/mcp/image_observer"
FIXTURE = RUNFILES / "tools/mcp/image_observer_fixture"
SERVER = RUNFILES / "tools/mcp/config_validation_server"
VALIDATOR = RUNFILES / "config/joshua_validate_config"
INSPECTOR = RUNFILES / "config/joshua_inspect_config"
CONFIG_PATH = "candidate.pbtxt"
CANDIDATE = (
    "\n"
    "robot {\n"
    "  perceptions {\n"
    "    single_perceptions {\n"
    '      sensor_name: "webcam"\n'
    "      sensor_type: IMAGE\n"
    "      node {\n"
    "        id: 12\n"
    "        node_type: CAMERA_PUBLISHER\n"
    '        publishers { ros2_data_type: IMAGE topic: "/_mcp_test/image" '
    "publish_rate_hz: 30 }\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "}\n"
)


class ImageSourceContractTest(unittest.IsolatedAsyncioTestCase):
    async def test_production_source_is_implemented(self):
        self.assertIsNotNone(
            importlib.util.find_spec("tools.mcp.image_source"),
            "production ImageSource is missing",
        )

    async def test_image_startup_requires_complete_safe_selection(self):
        for options in (
            ["--image-observer", str(HELPER)],
            ["--camera-id", "sensor:webcam"],
            ["--ros-config", CONFIG_PATH],
            ["--image-observer", str(HELPER), "--camera-id", "sensor:webcam"],
            [
                "--image-observer",
                str(HELPER),
                "--camera-id",
                "sensor:webcam",
                "--ros-config",
                "../candidate.pbtxt",
            ],
        ):
            process = await asyncio.create_subprocess_exec(
                str(SERVER),
                "--validator",
                str(VALIDATOR),
                *options,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            stdout, stderr = await asyncio.wait_for(process.communicate(), 5)
            self.assertEqual(process.returncode, 2, (options, stderr))
            self.assertEqual(stdout, b"")
            self.assertIn(b"--ros-config", stderr)

    async def test_no_runtime_close_and_descriptor(self):
        from tools.mcp.image_source import ImageSource
        from tools.mcp.observation_source import Availability, UnavailableReason

        with tempfile.TemporaryDirectory() as directory:
            Path(directory, CONFIG_PATH).write_text(CANDIDATE)
            source = ImageSource(directory, str(HELPER), "sensor:webcam", CONFIG_PATH)
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
    async def test_config_lookup_rejects_symlinks_without_touching_devices(self):
        from tools.mcp.image_source import ImageSource

        with (
            tempfile.TemporaryDirectory() as directory,
            tempfile.TemporaryDirectory() as outside,
        ):
            target = Path(outside, "candidate.pbtxt")
            target.write_text(CANDIDATE)
            Path(directory, "file.pbtxt").symlink_to(target)
            Path(directory, "nested").symlink_to(outside, target_is_directory=True)
            for path in ("file.pbtxt", "nested/candidate.pbtxt"):
                source = ImageSource(directory, str(HELPER), "sensor:webcam", path)
                try:
                    result = await source.read_latest()
                    self.assertEqual(result.availability.value, "unavailable", result)
                    self.assertIsNone(result.payload)
                finally:
                    await source.aclose()

    async def test_malformed_or_unbounded_config_has_no_image(self):
        from tools.mcp.image_source import ImageSource

        with tempfile.TemporaryDirectory() as directory:
            for content in ("not a protobuf", "x" * (1024 * 1024 + 1)):
                Path(directory, CONFIG_PATH).write_text(content)
                source = ImageSource(
                    directory, str(HELPER), "sensor:webcam", CONFIG_PATH
                )
                try:
                    result = await source.read_latest()
                    self.assertEqual(result.availability.value, "unavailable", result)
                    self.assertIsNone(result.payload)
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
                ("runtime_instance", "fabricated-runtime"),
                ("ros_domain", 233),
                ("ros_domain", 1),
                ("node_name", "invalid-node"),
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
                    runtime_instance=None,
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
                source = ImageSource(
                    directory, str(helper), "sensor:webcam", CONFIG_PATH
                )
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
        self.assertEqual(
            result.structuredContent["config_loaded_association"], "unknown"
        )
        self.assertEqual(result.structuredContent["runtime_association"], "unknown")
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
                "--ros-config",
                CONFIG_PATH,
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
                self.assertEqual(
                    image.outputSchema["properties"]["source_timestamp_semantics"][
                        "const"
                    ],
                    "source_header_time",
                )
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
                    "source_header_time",
                )
                for field in (
                    "acquisition_freshness",
                    "config_loaded_association",
                    "runtime_association",
                ):
                    self.assertEqual(result.structuredContent[field], "unknown")
                    self.assertEqual(
                        image.outputSchema["properties"][field]["const"], "unknown"
                    )
                self.assertIsNone(result.structuredContent["runtime_instance"])
                self.assertEqual(
                    image.outputSchema["properties"]["runtime_instance"]["type"], "null"
                )
                self.assertLessEqual(
                    len(result.model_dump_json(by_alias=True).encode()), 1024 * 1024
                )
                bad = await session.call_tool(
                    "get_camera_image", {"camera_id": "sensor:other"}
                )
                self.assertTrue(bad.isError)
                self.assertFalse(any(c.type == "image" for c in bad.content))
                self.assertEqual(
                    bad.structuredContent["config_loaded_association"], "unknown"
                )
                self.assertEqual(
                    bad.structuredContent["runtime_association"], "unknown"
                )
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
                for mode in ("padded", "bgr", "rebind_topic", "rebind_instance"):
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
                        self.assertIsNone(report["runtime_instance"])
                        self.assertNotEqual(report["sample_id"], before["sample_id"])
                        self.assertNotEqual(
                            report["publisher_gid"], before["publisher_gid"]
                        )
                    if mode == "rebind_topic":
                        self.assertNotEqual(report["topic"], before["topic"])
                await self.mode("restart_silent")
                await self.until(session, "unavailable")
                await self.mode("normal")
                await self.until(session, "fresh")
        self.assertIsNone(
            self.fixture.returncode, "MCP close stopped independent publisher"
        )

    async def test_binding_change_inside_encoder_rejects_old_bytes(self):
        from tools.mcp.image_source import ImageSource

        with patch.dict(os.environ, self.environment):
            source = ImageSource(
                self.directory,
                str(RUNFILES / "tools/mcp/image_observer_race_fixture"),
                "sensor:webcam",
                CONFIG_PATH,
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
                    self.directory,
                    str(RUNFILES / "tools/mcp" / executable),
                    "sensor:webcam",
                    CONFIG_PATH,
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
