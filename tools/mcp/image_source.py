"""One opt-in image reader. Only its isolated C++ helper uses ROS or codecs."""

import asyncio
import json
import os
import re
import struct
import time
from dataclasses import dataclass

from tools.mcp.config_validation_server import safe_reference
from tools.mcp.observation_source import (
    Availability,
    ObservationLimits,
    ObservationResult,
    ObservationSource,
    ObservationTime,
    SourceDescriptor,
    UnavailableReason,
)

MAX_METADATA = 16384
MAX_JPEG = 512 * 1024
READ_TIMEOUT = 5.0
CLEANUP_TIMEOUT = 2.0


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate metadata field")
        result[key] = value
    return result


def _jpeg_dimensions(data):
    """Check bounded JPEG segments and dimensions without a codec dependency."""
    if not data.startswith(b"\xff\xd8") or not data.endswith(b"\xff\xd9"):
        raise ValueError("Invalid JPEG envelope")
    position, dimensions = 2, None
    while position + 4 <= len(data):
        if data[position] != 255:
            raise ValueError("Invalid JPEG marker")
        while position < len(data) and data[position] == 255:
            position += 1
        if position >= len(data):
            break
        marker = data[position]
        position += 1
        if marker == 0xDA:
            if dimensions is None:
                raise ValueError("JPEG scan has no image dimensions")
            return dimensions
        if marker in (0xD8, 0xD9, 0x00) or 0xD0 <= marker <= 0xD7:
            raise ValueError("Unexpected JPEG marker")
        if position + 2 > len(data):
            break
        size = int.from_bytes(data[position : position + 2], "big")
        if size < 2 or position + size > len(data):
            raise ValueError("Truncated JPEG segment")
        if marker in (0xC0, 0xC1, 0xC2):
            if (
                size != 17
                or data[position + 2] != 8
                or data[position + 7] != 3
                or dimensions is not None
            ):
                raise ValueError("Unsupported JPEG frame")
            dimensions = (
                int.from_bytes(data[position + 5 : position + 7], "big"),
                int.from_bytes(data[position + 3 : position + 5], "big"),
            )
        position += size
    raise ValueError("JPEG has no complete image frame")


@dataclass(frozen=True)
class ImageFrame:
    jpeg: bytes
    width: int
    height: int
    source_width: int
    source_height: int
    source_encoding: str
    frame_id: str
    publisher_gid: str
    sample_id: str
    topic: str
    node_name: str
    node_namespace: str
    ros_domain: int
    mime_type: str = "image/jpeg"


class ImageSource(ObservationSource[ImageFrame]):
    """Owns one helper and query task; never stops the independent runtime."""

    def __init__(self, config_root, executable, camera_id, config_path):
        if (
            type(camera_id) is not str
            or not camera_id.startswith("sensor:")
            or not camera_id[7:].strip()
            or len(camera_id.encode("utf-8")) > 512
            or "\x00" in camera_id
        ):
            raise ValueError("camera-id must identify one configured sensor")
        if not safe_reference(config_path):
            raise ValueError("ros-config must be a safe relative config path")
        domain = os.environ.get("ROS_DOMAIN_ID", "0")
        if not re.fullmatch(r"[0-9]{1,3}", domain) or int(domain) > 232:
            raise ValueError("ROS_DOMAIN_ID must be an integer from 0 to 232")
        self._domain = int(domain)
        self._environment = dict(
            os.environ, ROS_DOMAIN_ID=domain, RCUTILS_LOGGING_USE_STDOUT="0"
        )
        self._config_path = config_path
        self._descriptor = SourceDescriptor(
            camera_id,
            "image",
            "joshua.camera.v1",
            encoding="image/jpeg",
            limits=ObservationLimits(5.0, 3.0, MAX_JPEG, 1),
        )
        self._fd = os.open(
            config_root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
        )
        self._executable = os.path.abspath(executable)
        self._closed = self._poisoned = False
        self._process = self._query = self._stderr_task = self._spawn_task = None
        self._query_started = False
        self._cleanup_task = None
        self._late_reaper = None
        self._stderr_size = self._counter = 0
        with open("/proc/sys/kernel/random/boot_id", encoding="ascii") as boot_file:
            self._clock = (
                "CLOCK_BOOTTIME:"
                + boot_file.read().strip()
                + ":"
                + os.readlink("/proc/self/ns/time")
            )

    def describe(self):
        return self._descriptor

    def _unavailable(self, reason, message):
        return ObservationResult(
            self.describe().source_id,
            Availability.UNAVAILABLE,
            diagnostic=message,
            unavailable_reason=reason,
        )

    async def _drain_stderr(self, stream):
        while True:
            data = await stream.read(4096)
            if not data:
                return
            self._stderr_size = min(65537, self._stderr_size + len(data))

    async def _start(self):
        if self._process is not None:
            return
        # Shield creation so cancellation cannot lose ownership of a spawned PID.
        spawn_fd = os.dup(self._fd)

        async def spawn():
            try:
                return await asyncio.create_subprocess_exec(
                    self._executable,
                    "--root-fd",
                    str(spawn_fd),
                    "--config",
                    self._config_path,
                    "--camera-id",
                    self.describe().source_id,
                    pass_fds=(spawn_fd,),
                    stdin=asyncio.subprocess.PIPE,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE,
                    limit=32768,
                    env=self._environment,
                )
            finally:
                os.close(spawn_fd)

        self._spawn_task = asyncio.create_task(spawn())
        try:
            self._process = await asyncio.shield(self._spawn_task)
        finally:
            if self._spawn_task.done() and not self._spawn_task.cancelled():
                self._process = self._spawn_task.result()
                self._spawn_task = None
        self._stderr_size = 0
        self._stderr_task = asyncio.create_task(
            self._drain_stderr(self._process.stderr)
        )

    def _reap_late(self, task):
        if task.cancelled() or task.exception() is not None:
            return
        process = task.result()

        async def reap():
            try:
                if process.returncode is None:
                    process.kill()
                await asyncio.wait_for(process.wait(), CLEANUP_TIMEOUT)
            except (OSError, asyncio.TimeoutError):
                pass

        self._late_reaper = asyncio.create_task(reap())

    async def _cleanup(self):
        deadline = asyncio.get_running_loop().time() + CLEANUP_TIMEOUT

        def remaining():
            return max(0, deadline - asyncio.get_running_loop().time())

        try:
            if self._spawn_task is not None:
                try:
                    self._process = await asyncio.wait_for(
                        asyncio.shield(self._spawn_task), remaining()
                    )
                except asyncio.TimeoutError:
                    self._spawn_task.add_done_callback(self._reap_late)
                    self._poisoned = True
                    return
                except OSError:
                    self._spawn_task = None
                    return
                self._spawn_task = None
            if self._process is not None:
                try:
                    if self._process.returncode is None:
                        self._process.kill()
                except ProcessLookupError:
                    pass
                await asyncio.wait_for(self._process.wait(), remaining())
                self._process = None
            if self._stderr_task is not None:
                self._stderr_task.cancel()
                await asyncio.wait_for(
                    asyncio.gather(self._stderr_task, return_exceptions=True),
                    remaining(),
                )
                self._stderr_task = None
        except (OSError, asyncio.TimeoutError):
            self._poisoned = True

    async def _stop(self):
        if (
            self._poisoned
            and self._cleanup_task is not None
            and self._cleanup_task.done()
        ):
            return
        if self._cleanup_task is None or self._cleanup_task.done():
            self._cleanup_task = asyncio.create_task(self._cleanup())
        await asyncio.shield(self._cleanup_task)

    @staticmethod
    async def _settle(task):
        # Repeated caller cancellation cannot interrupt source-owned cleanup.
        while not task.done():
            try:
                await asyncio.shield(task)
            except asyncio.CancelledError:
                pass
        return task.result()

    def _decode(self, meta, jpeg, query_id):
        common = {
            "query_id",
            "camera_id",
            "availability",
            "reason",
            "diagnostic",
            "reset",
        }
        sample = {
            "runtime_instance",
            "receipt_boottime_ns",
            "clock_id",
            "source_stamp_sec",
            "source_stamp_nanosec",
            "sample_id",
            "width",
            "height",
            "source_width",
            "source_height",
            "source_encoding",
            "frame_id",
            "publisher_gid",
            "topic",
            "node_name",
            "node_namespace",
            "ros_domain",
        }
        if (
            type(meta) is not dict
            or not common <= set(meta)
            or set(meta) - common - sample
        ):
            raise ValueError("Unexpected image metadata fields")
        if (
            type(meta["query_id"]) is not int
            or meta["query_id"] != query_id
            or meta["camera_id"] != self.describe().source_id
            or type(meta["reset"]) is not bool
        ):
            raise ValueError("Image response correlation mismatch")
        if (
            type(meta["diagnostic"]) is not str
            or type(meta["reason"]) is not str
            or type(meta["availability"]) is not str
        ):
            raise ValueError("Invalid common image metadata types")
        for key, value in meta.items():
            if isinstance(value, str) and (
                len(value.encode("utf-8")) > 1024 or "\x00" in value
            ):
                raise ValueError("Unbounded image metadata")
        if meta["availability"] == "unavailable":
            if jpeg or set(meta) != common:
                raise ValueError("Unavailable image response carries sample data")
            return self._unavailable(
                UnavailableReason(meta["reason"]), meta["diagnostic"]
            )
        if set(meta) != common | sample or meta["reason"] != "" or meta["reset"]:
            raise ValueError("Incomplete sample metadata")
        for key, maximum in (
            ("width", 640),
            ("height", 480),
            ("source_width", 1920),
            ("source_height", 1080),
        ):
            if type(meta[key]) is not int or not 1 <= meta[key] <= maximum:
                raise ValueError("Invalid image dimensions")
        if (
            meta["width"] > meta["source_width"]
            or meta["height"] > meta["source_height"]
            or meta["source_encoding"] not in ("rgb8", "bgr8")
        ):
            raise ValueError("Invalid image layout")
        if (
            type(meta["frame_id"]) is not str
            or len(meta["frame_id"].encode("utf-8")) > 256
        ):
            raise ValueError("Invalid image frame identifier")
        if (
            not 4 <= len(jpeg) <= MAX_JPEG
            or not jpeg.startswith(b"\xff\xd8")
            or not jpeg.endswith(b"\xff\xd9")
        ):
            raise ValueError("Invalid JPEG payload")
        if _jpeg_dimensions(jpeg) != (meta["width"], meta["height"]):
            raise ValueError("JPEG dimensions differ from metadata")
        if meta["clock_id"] != self._clock or not re.fullmatch(
            r"[0-9]{1,20}", meta["receipt_boottime_ns"]
        ):
            raise ValueError("Incomparable receipt clock")
        now = time.clock_gettime_ns(time.CLOCK_BOOTTIME)
        receipt = int(meta["receipt_boottime_ns"])
        if receipt > now:
            raise ValueError("Future receipt time")
        if meta["runtime_instance"] is not None:
            raise ValueError("ROS observation cannot identify a runtime instance")
        for key in (
            "sample_id",
            "publisher_gid",
            "topic",
            "node_name",
            "node_namespace",
        ):
            if not isinstance(meta[key], str) or not meta[key]:
                raise ValueError("Missing source association")
        if not re.fullmatch(
            r"(?:[0-9a-f]{32}|[0-9a-f]{48})", meta["publisher_gid"]
        ) or not re.fullmatch(r"[^:]{1,128}:[0-9]{1,20}", meta["sample_id"]):
            raise ValueError("Malformed publisher/sample identity")
        if (
            type(meta["ros_domain"]) is not int
            or meta["ros_domain"] != self._domain
            or not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", meta["node_name"])
            or meta["node_namespace"] != "/"
            or not re.fullmatch(r"(/[A-Za-z_][A-Za-z_0-9]*)+", meta["topic"])
        ):
            raise ValueError("Invalid source binding")
        sec, nano = meta["source_stamp_sec"], meta["source_stamp_nanosec"]
        if (
            type(sec) is not int
            or not -(2**31) <= sec < 2**31
            or type(nano) is not int
            or not 0 <= nano < 10**9
        ):
            raise ValueError("Invalid source stamp")
        availability = Availability(meta["availability"])
        if availability not in (Availability.FRESH, Availability.STALE):
            raise ValueError("Invalid sample availability")
        if now - receipt > 3_000_000_000:
            availability = Availability.STALE
        frame = ImageFrame(
            jpeg,
            **{
                key: meta[key]
                for key in ImageFrame.__dataclass_fields__
                if key not in ("jpeg", "mime_type")
            },
        )
        return ObservationResult(
            self.describe().source_id,
            availability,
            frame,
            ObservationTime(receipt, self._clock),
            ObservationTime(sec * 10**9 + nano),
            runtime_instance=None,
        )

    async def _read(self):
        try:
            await self._start()
            self._counter += 1
            self._process.stdin.write(f"{self._counter}\n".encode("ascii"))
            await self._process.stdin.drain()
            header = await self._process.stdout.readexactly(8)
            meta_size, jpeg_size = struct.unpack("!II", header)
            if not 0 < meta_size <= MAX_METADATA or jpeg_size > MAX_JPEG:
                raise ValueError("Oversized helper frame")
            meta = json.loads(
                (await self._process.stdout.readexactly(meta_size)).decode("utf-8"),
                object_pairs_hook=_pairs,
                parse_constant=lambda _: (_ for _ in ()).throw(
                    ValueError("Nonfinite metadata")
                ),
            )
            jpeg = await self._process.stdout.readexactly(jpeg_size)
            if self._stderr_size > 65536:
                raise ValueError("Helper stderr exceeded 64 KiB")
            result = self._decode(meta, jpeg, self._counter)
            if meta["reset"]:
                await self._stop()
            return result
        except asyncio.CancelledError:
            raise
        except (ValueError, KeyError, TypeError, UnicodeError, struct.error) as error:
            await self._stop()
            return self._unavailable(UnavailableReason.INVALID_DATA, str(error)[:512])
        except (OSError, asyncio.IncompleteReadError, BrokenPipeError) as error:
            await self._stop()
            return self._unavailable(
                UnavailableReason.BACKEND_ERROR,
                str(error)[:512] or "Image helper ended",
            )

    async def _bounded_read(self):
        self._query_started = True
        if self._closed:
            return self._unavailable(UnavailableReason.CLOSED, "Image reader closed")
        try:
            return await asyncio.wait_for(self._read(), READ_TIMEOUT)
        except asyncio.TimeoutError:
            cleanup = asyncio.create_task(self._stop())
            await self._settle(cleanup)
            if self._closed:
                return self._unavailable(
                    UnavailableReason.CLOSED, "Image reader closed"
                )
            return self._unavailable(
                UnavailableReason.TIMEOUT, "Image query exceeded five seconds"
            )
        except asyncio.CancelledError:
            cleanup = asyncio.create_task(self._stop())
            await self._settle(cleanup)
            if self._closed:
                return self._unavailable(
                    UnavailableReason.CLOSED, "Image reader closed"
                )
            raise

    async def read_latest(self):
        if self._closed:
            return self._unavailable(UnavailableReason.CLOSED, "Image reader closed")
        if self._poisoned:
            return self._unavailable(
                UnavailableReason.BACKEND_ERROR,
                "Helper cleanup unconfirmed; restart the MCP reader",
            )
        if self._query is not None and not self._query.done():
            return self._unavailable(
                UnavailableReason.BUSY, "One image query is already active"
            )
        self._query_started = False
        task = self._query = asyncio.create_task(self._bounded_read())
        try:
            return await asyncio.shield(task)
        except asyncio.CancelledError:
            task.cancel()
            try:
                await self._settle(task)
            except asyncio.CancelledError:
                pass
            raise
        finally:
            if self._query is task and task.done():
                self._query = None

    async def aclose(self):
        first_close = not self._closed
        self._closed = True
        query = self._query
        if query is not None and not query.done():
            # An unstarted coroutine cannot catch cancellation; let its closed
            # check resolve the pending read before it opens any helper resources.
            if first_close and self._query_started:
                query.cancel()
            try:
                await self._settle(query)
            except asyncio.CancelledError:
                pass
        cleanup = asyncio.create_task(self._stop())
        await self._settle(cleanup)
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None
