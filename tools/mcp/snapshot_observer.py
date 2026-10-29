"""Pinned read-only, bounded snapshot adapter. No ROS or runtime ownership."""

import json
import math
import os
import stat
import time

from tools.mcp.config_validation_server import STATUS_CODES, _unique_pairs
from tools.mcp.interfaces import RuntimeObserver

LIMITATIONS = [
    "Process presence is not readiness, connectivity or physical state.",
    (
        "ROS publisher metadata is association, not authentication "
        "or physical attestation."
    ),
    "Recent receipt does not establish acquisition freshness or task completion.",
    "ROS logs are partial; glog and unpublished logs are outside coverage.",
]
LEVELS = {"DEBUG": 10, "INFO": 20, "WARN": 30, "ERROR": 40, "FATAL": 50}
KEYS = {
    "schema_version",
    "scope",
    "operation",
    "status_code",
    "message",
    "availability",
    "snapshot_age_seconds",
    "instance_id",
    "snapshot_sequence",
    "data",
    "limitations",
}


OBSERVATION_SCHEMA = {
    "type": "object",
    "properties": {
        "schema_version": {"type": "integer", "const": 1},
        "scope": {"type": "string", "const": "runtime_observation"},
        "operation": {
            "type": "string",
            "enum": ["get_runtime_status", "get_recent_logs", "get_feedback"],
        },
        "status_code": {"type": "string", "enum": sorted(STATUS_CODES)},
        "message": {"type": "string"},
        "availability": {"type": "string", "enum": ["fresh", "stale", "unavailable"]},
        "snapshot_age_seconds": {"type": ["number", "null"], "minimum": 0},
        "instance_id": {"type": ["string", "null"]},
        "snapshot_sequence": {"type": ["integer", "null"], "minimum": 0},
        "data": {"type": "object"},
        "limitations": {"type": "array", "items": {"type": "string"}},
    },
    "required": sorted(KEYS),
    "additionalProperties": False,
}


def observation_error(operation, code, message):
    return dict(
        schema_version=1,
        scope="runtime_observation",
        operation=operation,
        status_code=code,
        message=message,
        availability="unavailable",
        snapshot_age_seconds=None,
        instance_id=None,
        snapshot_sequence=None,
        data={},
        limitations=list(LIMITATIONS),
    )


def validate_observation_report(report, operation):
    if (
        type(report) is not dict
        or set(report) != KEYS
        or type(report["schema_version"]) is not int
        or report["schema_version"] != 1
        or report["scope"] != "runtime_observation"
        or report["operation"] != operation
        or report["status_code"] not in STATUS_CODES
        or report["availability"] not in {"fresh", "stale", "unavailable"}
        or type(report["message"]) is not str
        or type(report["data"]) is not dict
        or report["limitations"] != LIMITATIONS
    ):
        raise ValueError("invalid observation result")
    age = report["snapshot_age_seconds"]
    if age is not None and (
        type(age) not in (int, float) or not math.isfinite(age) or age < 0
    ):
        raise ValueError("invalid observation age")
    return report


def _fields(value, fields):
    if type(value) is not dict or set(value) != set(fields.split()):
        raise ValueError("wrong snapshot fields")


def _string(value, limit=512):
    if type(value) is not str or len(value.encode("utf-8")) > limit or "\x00" in value:
        raise ValueError("invalid snapshot string")


def _integer(value, maximum=2**53 - 1):
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError("invalid snapshot integer")


def _ns(value):
    if (
        type(value) is not str
        or not value
        or len(value) > 20
        or not value.isascii()
        or not value.isdecimal()
        or int(value) > 2**64 - 1
    ):
        raise ValueError("invalid monotonic time")
    return int(value)


def validate_snapshot(s):
    _fields(
        s,
        (
            "schema_version instance_id sequence boot_id time_namespace "
            "clock_kind captured_boottime_ns build_version config phase "
            "expected_process_count processes omitted_processes exits "
            "omitted_exits observation logs feedback"
        ),
    )
    if (
        type(s["schema_version"]) is not int
        or s["schema_version"] != 1
        or s["clock_kind"] != "CLOCK_BOOTTIME"
    ):
        raise ValueError("unsupported snapshot")
    for key in ("instance_id", "boot_id", "time_namespace", "build_version", "phase"):
        _string(s[key])
    if not s["instance_id"] or not s["boot_id"] or not s["time_namespace"]:
        raise ValueError("missing snapshot identity")
    if s["phase"] not in {"monitoring", "shutdown_requested", "monitor_finished"}:
        raise ValueError("invalid runtime phase")
    for key in (
        "sequence",
        "expected_process_count",
        "omitted_processes",
        "omitted_exits",
    ):
        _integer(s[key])
    _ns(s["captured_boottime_ns"])
    _fields(s["config"], "source name id provenance")
    for key, val in s["config"].items():
        _string(val, 4096 if key == "source" else 512)
    _fields(s["observation"], "state error")
    for val in s["observation"].values():
        _string(val)
    if s["observation"]["state"] not in {"prepared", "observing", "disabled"}:
        raise ValueError("invalid observer state")
    for collection in ("processes", "exits"):
        if type(s[collection]) is not list or len(s[collection]) > 64:
            raise ValueError("unbounded processes")
        for item in s[collection]:
            _fields(
                item,
                (
                    "pid node_id node_name node_type exit_code signal"
                    if collection == "exits"
                    else "pid node_id node_name node_type"
                ),
            )
            for k in ("pid", "node_id"):
                _integer(item[k])
            for k in ("node_name", "node_type"):
                _string(item[k])
            if collection == "exits":
                for k in ("exit_code", "signal"):
                    if item[k] is not None:
                        _integer(item[k], 255)
    logs = s["logs"]
    _fields(logs, "entries dropped truncated latest_sequence")
    for k in ("dropped", "truncated", "latest_sequence"):
        _integer(logs[k])
    if type(logs["entries"]) is not list or len(logs["entries"]) > 64:
        raise ValueError("unbounded logs")
    previous = 0
    for entry in logs["entries"]:
        _fields(
            entry,
            (
                "sequence level name message file function line stamp_sec "
                "stamp_nanosec receipt_boottime_ns runtime_association"
            ),
        )
        for k in ("sequence", "level", "line", "stamp_nanosec"):
            _integer(entry[k])
        if type(entry["stamp_sec"]) is not int:
            raise ValueError("invalid source timestamp")
        if (
            not previous < entry["sequence"] <= logs["latest_sequence"]
            or entry["stamp_nanosec"] >= 10**9
            or _ns(entry["receipt_boottime_ns"]) > _ns(s["captured_boottime_ns"])
        ):
            raise ValueError("invalid log ordering/time")
        previous = entry["sequence"]
        for k in ("name", "message", "file", "function", "runtime_association"):
            _string(entry[k], 512 if k == "message" else 256)
        if entry["runtime_association"] != "runtime_association_unverified":
            raise ValueError("unsupported log association")
    f = s["feedback"]
    _fields(f, "sensor_id state source sample limitations")
    _string(f["sensor_id"])
    _string(f["state"])
    if f["state"] not in {
        "not_configured",
        "unavailable",
        "missing_publisher",
        "wrong_publisher",
        "ambiguous",
        "invalid",
        "available",
    }:
        raise ValueError("invalid feedback state")
    _fields(f["source"], "topic node_name node_namespace board_name channel")
    for k, val in f["source"].items():
        if k == "channel":
            _integer(val)
        else:
            _string(val)
    if (
        type(f["limitations"]) is not list
        or len(f["limitations"]) > 8
        or any(type(v) is not str or len(v) > 512 for v in f["limitations"])
    ):
        raise ValueError("invalid feedback limitations")
    sample = f["sample"]
    if sample is not None:
        _fields(
            sample,
            (
                "position units reference source_stamp_sec "
                "source_stamp_nanosec frame_id receipt_boottime_ns "
                "publisher_gid acquisition_freshness"
            ),
        )
        if (
            f["state"] != "available"
            or type(sample["position"]) not in (int, float)
            or abs(sample["position"]) > 1e9
            or not math.isfinite(sample["position"])
            or sample["units"] != "radians"
            or sample["reference"] != "device_zero"
            or sample["acquisition_freshness"] != "unknown"
        ):
            raise ValueError("invalid measurement")
        if type(sample["source_stamp_sec"]) is not int:
            raise ValueError("invalid source stamp")
        _integer(sample["source_stamp_nanosec"], 10**9 - 1)
        for k in ("frame_id", "publisher_gid"):
            _string(sample[k], 256)
        # RMW stores 16 GID bytes in Jazzy and 24 in Humble. Preserve the
        # producer's complete identity without accepting arbitrary widths.
        if len(sample["publisher_gid"]) not in (32, 48) or any(
            c not in "0123456789abcdef" for c in sample["publisher_gid"]
        ):
            raise ValueError("invalid publisher GID")
        if _ns(sample["receipt_boottime_ns"]) > _ns(s["captured_boottime_ns"]):
            raise ValueError("future receipt")
    elif f["state"] == "available":
        raise ValueError("missing measurement")
    return s


class SnapshotObserver(RuntimeObserver):
    def __init__(self, directory=None, freshness_seconds=3.0):
        if (
            type(freshness_seconds) not in (int, float)
            or not math.isfinite(freshness_seconds)
            or not 0 < freshness_seconds <= 60
        ):
            raise ValueError("Freshness must be between zero and sixty seconds.")
        self.freshness = freshness_seconds
        self._fd = None
        with open("/proc/sys/kernel/random/boot_id", encoding="ascii") as boot_file:
            self._boot = boot_file.read().strip()
        self._namespace = os.readlink("/proc/self/ns/time")
        if directory is not None:
            fd = os.open(
                directory, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
            )
            info = os.fstat(fd)
            if info.st_uid != os.geteuid() or info.st_mode & 0o077:
                os.close(fd)
                raise ValueError(
                    "Observation directory must be private and owned by this UID."
                )
            self._fd = fd

    async def aclose(self):
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None

    def _read(self, operation):
        if self._fd is None:
            return None, observation_error(
                operation,
                "UNAVAILABLE",
                "Passive observation directory is not configured or adapter is closed.",
            )
        try:
            pin = os.open(
                "snapshot.json",
                os.O_PATH | os.O_CLOEXEC | os.O_NOFOLLOW,
                dir_fd=self._fd,
            )
            try:
                info = os.fstat(pin)
                if (
                    not stat.S_ISREG(info.st_mode)
                    or info.st_uid != os.geteuid()
                    or info.st_mode & 0o077
                    or info.st_nlink != 1
                    or not 0 < info.st_size <= 65536
                ):
                    raise ValueError("unsafe snapshot file")
                fd = os.open(
                    f"/proc/self/fd/{pin}", os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC
                )
                try:
                    content_info = os.fstat(fd)
                    if (content_info.st_dev, content_info.st_ino) != (
                        info.st_dev,
                        info.st_ino,
                    ) or not stat.S_ISREG(content_info.st_mode):
                        raise ValueError("snapshot identity changed")
                    raw = os.read(fd, 65537)
                    if len(raw) > 65536:
                        raise ValueError("oversized snapshot")
                finally:
                    os.close(fd)
            finally:
                os.close(pin)
            snapshot = validate_snapshot(
                json.loads(
                    raw.decode("utf-8"),
                    object_pairs_hook=_unique_pairs,
                    parse_constant=lambda _: (_ for _ in ()).throw(
                        ValueError("nonfinite JSON")
                    ),
                )
            )
            now = time.clock_gettime_ns(time.CLOCK_BOOTTIME)
            captured = _ns(snapshot["captured_boottime_ns"])
            if (
                snapshot["boot_id"] != self._boot
                or snapshot["time_namespace"] != self._namespace
                or captured > now
            ):
                raise ValueError("snapshot clock identity or future timestamp")
            age = (now - captured) / 1e9
            report = observation_error(
                operation,
                "OK",
                "Passive snapshot read; phase describes the last publication.",
            )
            report.update(
                availability="fresh" if age <= self.freshness else "stale",
                snapshot_age_seconds=age,
                instance_id=snapshot["instance_id"],
                snapshot_sequence=snapshot["sequence"],
            )
            return snapshot, report
        except FileNotFoundError:
            return None, observation_error(
                operation, "UNAVAILABLE", "No passive snapshot has been published."
            )
        except (OSError, ValueError, UnicodeError, RecursionError, KeyError, TypeError):
            return None, observation_error(
                operation,
                "DATA_LOSS",
                (
                    "Passive snapshot is unsafe, malformed or has an "
                    "incompatible clock identity."
                ),
            )

    async def get_runtime_status(self):
        snapshot, report = self._read("get_runtime_status")
        if snapshot is not None:
            report["data"] = {
                k: snapshot[k]
                for k in (
                    "phase",
                    "config",
                    "build_version",
                    "expected_process_count",
                    "processes",
                    "omitted_processes",
                    "exits",
                    "omitted_exits",
                    "observation",
                )
            }
            report["data"]["tracked_process_count"] = (
                len(snapshot["processes"]) + snapshot["omitted_processes"]
            )
        return report

    async def get_recent_logs(self, cursor=None, limit=50, min_level="WARN"):
        operation = "get_recent_logs"
        if (
            type(limit) is not int
            or not 1 <= limit <= 100
            or type(min_level) is not str
            or min_level not in LEVELS
        ):
            return observation_error(
                operation,
                "INVALID_ARGUMENT",
                "Require limit 1..100 and a known minimum severity.",
            )
        if cursor is not None:
            try:
                _fields(cursor, "instance_id sequence")
                _string(cursor["instance_id"])
                _integer(cursor["sequence"])
                if not cursor["instance_id"]:
                    raise ValueError("empty cursor instance")
            except (ValueError, UnicodeError):
                return observation_error(
                    operation,
                    "INVALID_ARGUMENT",
                    "Require an instance_id and nonnegative sequence cursor.",
                )
        s, report = self._read(operation)
        if s is None:
            return report
        logs = s["logs"]
        reset = cursor is not None and cursor["instance_id"] != s["instance_id"]
        after = 0 if cursor is None or reset else cursor["sequence"]
        if after > logs["latest_sequence"]:
            return observation_error(
                operation,
                "INVALID_ARGUMENT",
                "Cursor sequence is ahead of this runtime instance.",
            )
        first = (
            logs["entries"][0]["sequence"]
            if logs["entries"]
            else logs["latest_sequence"] + 1
        )
        gap = max(0, first - after - 1)
        entries, examined = [], after
        for entry in logs["entries"]:
            if entry["sequence"] <= after:
                continue
            examined = entry["sequence"]
            if entry["level"] >= LEVELS[min_level]:
                entries.append(entry)
                if len(entries) == limit:
                    break
        else:
            examined = logs["latest_sequence"]
        report["data"] = dict(
            entries=entries,
            cursor=dict(instance_id=s["instance_id"], sequence=examined),
            cursor_reset=reset,
            retention_gap=gap,
            dropped=logs["dropped"],
            truncated=logs["truncated"],
            coverage=(
                "Configured logger-name matches only; "
                "runtime_association_unverified. Partial /rosout history."
            ),
        )
        return report

    async def get_feedback(self, sensor_id):
        if type(sensor_id) is not str or not sensor_id or len(sensor_id) > 512:
            return observation_error(
                "get_feedback",
                "INVALID_ARGUMENT",
                "Require the configured sensor identity.",
            )
        s, report = self._read("get_feedback")
        if s is None:
            return report
        f = s["feedback"]
        if not f["sensor_id"] or f["sensor_id"] != sensor_id:
            report["data"] = dict(
                sensor_id=sensor_id, state="not_configured", sample=None
            )
        else:
            report["data"] = dict(f)
            if f["sample"] is not None:
                age = (
                    time.clock_gettime_ns(time.CLOCK_BOOTTIME)
                    - _ns(f["sample"]["receipt_boottime_ns"])
                ) / 1e9
                report["data"]["receipt_age_seconds"] = age
                if report["availability"] != "fresh" or age > self.freshness:
                    report["data"]["state"] = "stale"
        return report
