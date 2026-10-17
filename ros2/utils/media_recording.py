"""Continuous encoded A/V recording with filesystem-aware, oldest-first retention.

A separate, listener-free MediaMTX reads the live loopback RTSP stream. Stopping
this process for storage pressure never stops the live streaming server.
"""

import fcntl
import json
import logging
import os
import re
import shlex
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

LOG = logging.getLogger(__name__)
OWNER = "joshua-continuous-recording-v1\n"
RUN = re.compile(r"run-[0-9a-f]{32}")
CLIP = re.compile(r"recording-[0-9]{8}T[0-9]{6}-[0-9]{6}\.mp4")


def validate_recording(options):
    directory = options.recording_directory
    if directory and not (directory.startswith("/") or directory.startswith("~/")):
        raise ValueError("Recording directory must be absolute or start with ~/")
    if "%" in directory or "\n" in directory or "\r" in directory:
        raise ValueError("Recording directory cannot contain % or newlines")
    if options.recording_segment_sec and not 2 <= options.recording_segment_sec <= 600:
        raise ValueError("Recording segments must be 2..600 seconds")
    if (
        options.recording_reserve_percent
        and not 20 <= options.recording_reserve_percent <= 90
    ):
        raise ValueError("Recording reserve must be 20..90 percent")


class RecordingStore:
    """Exclusive ownership; never follow symlinks or prune a currently open clip."""

    def __init__(self, directory, reserve_percent, headroom):
        self.root = Path(directory).expanduser()
        if self.root.is_symlink():
            raise ValueError("Recording directory must not be a symlink")
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.root = self.root.resolve()
        marker = self.root / ".joshua-recordings"
        if not marker.exists() and any(self.root.iterdir()):
            raise ValueError("Recording directory must be empty or Joshua-managed")
        self.lock = os.open(marker, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        try:
            fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            value = os.read(self.lock, 128).decode()
            if value and value != OWNER:
                raise ValueError("Unknown recording directory ownership marker")
            if not value:
                os.write(self.lock, OWNER.encode())
                os.fsync(self.lock)
        except Exception:
            os.close(self.lock)
            raise
        self.reserve_percent = reserve_percent
        self.headroom = headroom
        self.current = None
        self.deleted = 0

    def clips(self):
        clips = []
        for run in self.root.iterdir():
            if not RUN.fullmatch(run.name) or run.is_symlink() or not run.is_dir():
                continue
            for clip in run.iterdir():
                if (
                    CLIP.fullmatch(clip.name)
                    and not clip.is_symlink()
                    and clip.is_file()
                ):
                    clips.append(clip)
        return clips

    def space(self):
        usage = os.statvfs(self.root)
        total = usage.f_blocks * usage.f_frsize
        # bavail excludes blocks reserved for root; those do not count as usable.
        available = usage.f_bavail * usage.f_frsize
        reserve = (total * self.reserve_percent + 99) // 100
        return available, reserve

    def prune(self):
        available, reserve = self.space()
        target = reserve + self.headroom
        if available < target:
            candidates = [
                p
                for p in self.clips()
                if p.parent != self.current or p.with_suffix(".complete").is_file()
            ]
            for clip in sorted(
                candidates, key=lambda p: (p.stat().st_mtime_ns, str(p))
            ):
                if available >= target:
                    break
                clip.unlink()
                clip.with_suffix(".complete").unlink(missing_ok=True)
                self.deleted += 1
                available, reserve = self.space()
            # Empty sessions accumulate after restarts; never remove foreign files.
            for run in self.root.iterdir():
                if (
                    RUN.fullmatch(run.name)
                    and not run.is_symlink()
                    and run != self.current
                ):
                    try:
                        run.rmdir()
                    except OSError:
                        pass
        return available >= target, available, reserve

    def close(self):
        os.close(self.lock)


def recorder_config(options, session):
    command = shlex.join([sys.executable, str(Path(__file__).resolve()), "--complete"])
    config = {
        name: False
        for name in (
            "rtsp",
            "rtmp",
            "hls",
            "webrtc",
            "srt",
            "moq",
            "api",
            "metrics",
            "pprof",
            "playback",
        )
    }
    config.update(
        {
            "logLevel": "warn",
            "paths": {
                "recording": {
                    "source": (
                        f"rtsp://127.0.0.1:{options.rtsp_port}/{options.stream_name}"
                    ),
                    "rtspTransport": "tcp",
                    "sourceOnDemand": False,
                    "record": True,
                    "recordPath": str(session / "%path-%Y%m%dT%H%M%S-%f"),
                    "recordFormat": "fmp4",
                    "recordPartDuration": "1s",
                    "recordMaxPartSize": "16M",
                    "recordSegmentDuration": f"{options.recording_segment_sec or 120}s",
                    "recordDeleteAfter": "0s",
                    "runOnRecordSegmentComplete": command,
                }
            },
        }
    )
    return config


class Recorder:
    def __init__(self, options, binary):
        validate_recording(options)
        self.options = options
        self.binary = binary
        # Keep more than a full segment plus bursts in addition to the reserve.
        headroom = max(
            512 * 1024**2,
            (options.bitrate_kbps + 64)
            * 1000
            // 8
            * (options.recording_segment_sec or 120)
            * 2,
        )
        self.store = RecordingStore(
            options.recording_directory,
            options.recording_reserve_percent or 20,
            headroom,
        )
        self.process = None
        self.stop = threading.Event()
        self.thread = None
        self.status = {"state": "starting"}

    def start(self):
        self.thread = threading.Thread(
            target=self.run, name="media-recording", daemon=True
        )
        self.thread.start()

    def start_process(self):
        session = self.store.root / ("run-" + uuid.uuid4().hex)
        session.mkdir(mode=0o700)
        self.store.current = session
        config = self.store.root / "recorder.json"
        config.write_text(json.dumps(recorder_config(self.options, session)))
        # Retain the ownership lock if the parent is killed but the child survives.
        self.process = subprocess.Popen(
            [self.binary, str(config)], pass_fds=(self.store.lock,)
        )
        self.started = time.monotonic()

    def stop_process(self):
        if self.process:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
            self.process = None
        # Once reaped, even an interrupted final segment is safe to expire.
        self.store.current = None

    def publish_status(self, state, available, reserve, detail=""):
        if self.status.get("state") != state:
            LOG.warning("Recording %s: %s", state, detail)
        self.status = {
            "state": state,
            "detail": detail,
            "observed_at_ns": time.time_ns(),
            "available_bytes": available,
            "reserve_bytes": reserve,
            "reserve_percent": self.store.reserve_percent,
            "deleted_clips": self.store.deleted,
        }
        target = self.store.root / "status.json"
        temporary = target.with_suffix(".tmp")
        temporary.write_text(json.dumps(self.status) + "\n")
        temporary.replace(target)

    def tick(self):
        ready, available, reserve = self.store.prune()
        if not ready:
            self.stop_process()
            # The former active file can now be expired if necessary.
            _, available, reserve = self.store.prune()
            self.publish_status(
                "paused_low_space",
                available,
                reserve,
                "Waiting for free space above reserve plus safety margin",
            )
            return
        if self.process and self.process.poll() is not None:
            code = self.process.returncode
            self.stop_process()
            raise RuntimeError(f"Recording MediaMTX exited ({code})")
        if self.process is None:
            self.start_process()
        clips = [p for p in self.store.clips() if p.parent == self.store.current]
        newest = max((p.stat().st_mtime for p in clips), default=0)
        growing = time.time() - newest < 15
        if not growing and time.monotonic() - self.started > 30:
            self.stop_process()
            raise RuntimeError("No fresh recorded data for 30 seconds; reconnecting")
        self.publish_status(
            "recording" if growing else "waiting_for_stream", available, reserve
        )

    def run(self):
        try:
            while not self.stop.is_set():
                try:
                    self.tick()
                except Exception as error:
                    self.stop_process()
                    self.status = {
                        "state": "error",
                        "detail": str(error),
                        "observed_at_ns": time.time_ns(),
                    }
                    LOG.exception("Recording paused; live streaming continues")
                self.stop.wait(2)
        finally:
            self.stop_process()

    def close(self):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=10)
            if self.thread.is_alive():
                raise RuntimeError("Recording worker did not shut down")
        self.store.close()


def mark_complete():
    # Called only after MediaMTX closes a segment. Unmarked active segments are
    # never deleted; old process sessions become eligible after the lock is free.
    clip = Path(os.environ["MTX_SEGMENT_PATH"])
    if CLIP.fullmatch(clip.name) and RUN.fullmatch(clip.parent.name):
        if not clip.is_symlink() and clip.is_file():
            clip.with_suffix(".complete").touch(mode=0o600)


if __name__ == "__main__":
    if sys.argv[1:] != ["--complete"]:
        raise SystemExit("This module is invoked by MediaMTX's completion hook")
    mark_complete()
