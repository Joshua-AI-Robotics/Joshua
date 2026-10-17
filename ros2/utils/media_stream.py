"""Bounded ROS-independent GStreamer bridge and private MediaMTX lifecycle."""

import ipaddress
import json
import os
import re
import socket
import subprocess
import tempfile
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Sample:
    kind: str
    stamp_ns: int
    data: bytes
    caps: str
    duration_ns: int
    discontinuity: bool = False


def private_address(value):
    if not value:
        value = subprocess.check_output(
            ["tailscale", "ip", "-4"], text=True, timeout=5
        ).strip()
    address = ipaddress.IPv4Address(value)
    if not (address.is_loopback or address in ipaddress.ip_network("100.64.0.0/10")):
        raise ValueError("Media listener must be a Tailscale or loopback IPv4 address")
    return str(address)


def validate_options(options):
    if (
        not 16 <= options.width <= 1920
        or not 16 <= options.height <= 1080
        or options.width % 2
        or options.height % 2
        or not 1 <= options.fps <= 30
        or not 100 <= options.bitrate_kbps <= 10000
        or not re.fullmatch(r"[A-Za-z0-9_-]+", options.stream_name)
    ):
        raise ValueError("Invalid media dimensions, FPS, bitrate, or stream name")
    ports = (options.http_port, options.ice_port, options.rtsp_port)
    if len(set(ports)) != 3 or any(not 1024 <= port <= 65535 for port in ports):
        raise ValueError("Media ports must be distinct and between 1024 and 65535")
    if options.listen_address:
        private_address(options.listen_address)


def server_config(options, address):
    """JSON is valid YAML; avoid interpolation into YAML or shell commands."""
    return {
        "logLevel": "warn",
        # Leave room for the Tailscale/WireGuard tunnel MTU.
        "udpMaxPayloadSize": 1200,
        "rtspAddress": f"127.0.0.1:{options.rtsp_port}",
        "rtspTransports": ["tcp"],
        "rtmp": False,
        "hls": False,
        "srt": False,
        "moq": False,
        "api": False,
        "metrics": False,
        "pprof": False,
        "playback": False,
        "webrtc": True,
        "webrtcAddress": f"{address}:{options.http_port}",
        "webrtcLocalUDPAddress": f"{address}:{options.ice_port}",
        "webrtcLocalTCPAddress": f"{address}:{options.ice_port}",
        "webrtcIPsFromInterfaces": False,
        "webrtcAdditionalHosts": [address],
        "webrtcICEServers2": [],
        "webrtcAllowOrigins": [f"http://{address}:{options.http_port}"],
        "authInternalUsers": [
            {
                "user": "any",
                "ips": ["127.0.0.0/8"],
                "permissions": [{"action": "publish", "path": options.stream_name}],
            },
            {
                "user": "any",
                "ips": ["100.64.0.0/10", "127.0.0.0/8"],
                "permissions": [{"action": "read", "path": options.stream_name}],
            },
        ],
        "paths": {options.stream_name: {"source": "publisher"}},
    }


class PendingSamples:
    """One latest video frame and at most 200 ms of audio, never blocking ROS."""

    def __init__(self):
        self.condition = threading.Condition()
        self.video = None
        self.audio = deque()
        self.audio_ns = 0
        self.dropped_video = 0
        self.dropped_audio = 0

    def put(self, sample):
        with self.condition:
            if sample.kind == "video":
                self.dropped_video += self.video is not None
                self.video = sample
            else:
                self.audio.append(sample)
                self.audio_ns += sample.duration_ns
                dropped = False
                while self.audio and self.audio_ns > 200_000_000:
                    self.audio_ns -= self.audio.popleft().duration_ns
                    self.dropped_audio += 1
                    dropped = True
                if dropped and self.audio:
                    # Timestamp continuity below also detects appsrc-side drops.
                    self.audio[0].discontinuity = True
            self.condition.notify()

    def take(self):
        with self.condition:
            if not self.audio and self.video is None:
                self.condition.wait(0.05)
            samples = list(self.audio)
            self.audio.clear()
            self.audio_ns = 0
            if self.video:
                samples.append(self.video)
                self.video = None
            return sorted(samples, key=lambda sample: sample.stamp_ns)


class MediaStream:
    def __init__(self, options, audio, report, binary=None):
        validate_options(options)
        self.options = options
        self.address = private_address(options.listen_address)
        self.audio = audio
        self.report = report
        self.binary = binary or str(Path(__file__).with_name("mediamtx"))
        self.pending = PendingSamples()
        self.stop = threading.Event()
        self.thread = None
        self.server = None
        self.pipeline = None
        self.sources = {}
        self.formats = {}
        self.last_input = {}
        self.last_pts = {}
        self.last_end = {}
        self.last_state = None
        self.url = f"http://{self.address}:{options.http_port}/{options.stream_name}/"
        self.directory = tempfile.TemporaryDirectory(prefix="joshua-media-")
        self.config_path = Path(self.directory.name) / "mediamtx.json"
        self.config_path.write_text(json.dumps(server_config(options, self.address)))

    def state(self, state, detail=""):
        if (state, detail) != self.last_state:
            self.last_state = (state, detail)
            self.report(state, detail)

    def start(self):
        # Import only in the streaming process. Ubuntu GI must match the selected
        # Humble/Python 3.10 or Jazzy/Python 3.12 interpreter, like rclpy.
        # RTSP ingest is always loopback. Bypass desktop proxy discovery, which
        # can load libproxy/libunwind into headless GStreamer and crash on u22.
        os.environ["GIO_USE_PROXY_RESOLVER"] = "dummy"
        import gi

        gi.require_version("Gst", "1.0")
        from gi.repository import Gst

        Gst.init(None)
        for plugin in (
            "appsrc",
            "videoconvert",
            "videoscale",
            "videorate",
            "x264enc",
            "h264parse",
            "rtspclientsink",
            "audioconvert",
            "audioresample",
            "opusenc",
        ):
            if Gst.ElementFactory.find(plugin) is None:
                raise RuntimeError(
                    f"Missing GStreamer element: {plugin}; see BABY_MONITOR.md"
                )
        self.Gst = Gst
        self.thread = threading.Thread(
            target=self.run, name="media-stream", daemon=True
        )
        self.thread.start()

    def submit(self, sample):
        self.pending.put(sample)

    def start_server(self):
        self.server = subprocess.Popen([self.binary, str(self.config_path)])
        deadline = time.monotonic() + 5
        while not self.stop.is_set() and time.monotonic() < deadline:
            if self.server.poll() is not None:
                raise RuntimeError(f"MediaMTX exited ({self.server.returncode})")
            try:
                with socket.create_connection(
                    ("127.0.0.1", self.options.rtsp_port), 0.1
                ):
                    return
            except OSError:
                self.stop.wait(0.1)
        raise RuntimeError("MediaMTX did not open the RTSP ingest port")

    def start_pipeline(self):
        Gst = self.Gst
        c = self.options
        # Sink properties are assigned below rather than interpolating URLs.
        video = (
            "appsrc name=video is-live=true format=time block=false "
            "max-buffers=1 max-bytes=0 leaky-type=downstream ! "
            "videoconvert ! videoscale ! videorate drop-only=true ! "
            f"video/x-raw,format=I420,width={c.width},height={c.height},"
            f"framerate={c.fps}/1 ! "
            f"x264enc tune=zerolatency speed-preset=ultrafast bitrate={c.bitrate_kbps} "
            f"key-int-max={c.fps} bframes=0 threads=2 ! "
            "video/x-h264,profile=baseline ! h264parse config-interval=-1 ! sink. "
        )
        audio = (
            "appsrc name=audio is-live=true format=time block=false "
            "max-time=200000000 max-bytes=0 leaky-type=downstream ! "
            "audioconvert ! audioresample ! audio/x-raw,rate=48000,channels=1 ! "
            "opusenc bitrate=32000 frame-size=20 ! sink. "
            if self.audio
            else ""
        )
        self.pipeline = Gst.parse_launch(
            "rtspclientsink name=sink protocols=tcp latency=0 " + video + audio
        )
        self.pipeline.get_by_name("sink").set_property(
            "location", f"rtsp://127.0.0.1:{c.rtsp_port}/{c.stream_name}"
        )
        self.sources = {}
        for kind, caps in self.formats.items():
            source = self.pipeline.get_by_name(kind)
            source.set_property("caps", Gst.Caps.from_string(caps))
            self.sources[kind] = source
        self.anchor_ns = time.time_ns()
        self.started_at = time.monotonic()
        self.last_probe = 0
        self.last_pts.clear()
        self.last_end.clear()
        if self.pipeline.set_state(Gst.State.PLAYING) == Gst.StateChangeReturn.FAILURE:
            raise RuntimeError("GStreamer could not enter PLAYING")
        self.state("starting", self.url)

    def stream_ready(self):
        # PLAYING is reached before RTSP RECORD completes. Advertise readiness
        # only once a reader can DESCRIBE the encoded stream.
        try:
            with socket.create_connection(
                ("127.0.0.1", self.options.rtsp_port), 0.2
            ) as sock:
                request = (
                    f"DESCRIBE rtsp://127.0.0.1:{self.options.rtsp_port}/"
                    f"{self.options.stream_name} RTSP/1.0\r\n"
                    "CSeq: 1\r\nAccept: application/sdp\r\n\r\n"
                )
                sock.sendall(request.encode())
                return sock.recv(4096).startswith(b"RTSP/1.0 200 ")
        except OSError:
            return False

    def stop_pipeline(self):
        if self.pipeline:
            self.pipeline.set_state(self.Gst.State.NULL)
            self.pipeline = None
        self.sources.clear()

    def stop_server(self):
        if self.server:
            if self.server.poll() is None:
                self.server.terminate()
                try:
                    self.server.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.server.kill()
                    self.server.wait()
            self.server = None

    def run(self):
        Gst = self.Gst
        required = {"video", "audio"} if self.audio else {"video"}
        try:
            while not self.stop.is_set():
                try:
                    if self.server is None:
                        self.start_server()
                        self.state("waiting_for_input", self.url)
                    if self.server.poll() is not None:
                        raise RuntimeError("MediaMTX stopped unexpectedly")
                    samples = self.pending.take()
                    for sample in samples:
                        age = time.time_ns() - sample.stamp_ns
                        if age > 500_000_000 or age < -100_000_000:
                            continue
                        if self.formats.get(sample.kind, sample.caps) != sample.caps:
                            self.stop_pipeline()
                        self.formats[sample.kind] = sample.caps
                        self.last_input[sample.kind] = time.monotonic()
                    fresh = all(
                        time.monotonic() - self.last_input.get(kind, 0) < 3
                        for kind in required
                    )
                    if not fresh:
                        if self.pipeline:
                            self.stop_pipeline()
                            self.state(
                                "input_stale",
                                "Waiting for fresh video and configured audio",
                            )
                        continue
                    if self.pipeline is None:
                        self.start_pipeline()
                    for sample in samples:
                        age = time.time_ns() - sample.stamp_ns
                        pts = sample.stamp_ns - self.anchor_ns
                        if (
                            age > 500_000_000
                            or age < -100_000_000
                            or pts < 0
                            or pts <= self.last_pts.get(sample.kind, -1)
                        ):
                            continue
                        buffer = Gst.Buffer.new_allocate(None, len(sample.data), None)
                        buffer.fill(0, sample.data)
                        buffer.pts = pts
                        buffer.duration = sample.duration_ns
                        previous_end = self.last_end.get(sample.kind, pts)
                        if sample.discontinuity or (
                            sample.kind == "audio"
                            and abs(pts - previous_end) > 5_000_000
                        ):
                            buffer.set_flags(Gst.BufferFlags.DISCONT)
                        result = self.sources[sample.kind].emit("push-buffer", buffer)
                        if result != Gst.FlowReturn.OK:
                            raise RuntimeError(
                                f"GStreamer rejected {sample.kind}: {result}"
                            )
                        self.last_pts[sample.kind] = pts
                        self.last_end[sample.kind] = pts + sample.duration_ns
                    bus = self.pipeline.get_bus()
                    while True:
                        message = bus.pop()
                        if message is None:
                            break
                        if message.type == Gst.MessageType.ERROR:
                            error, debug = message.parse_error()
                            raise RuntimeError(f"{error.message}: {debug}")
                        if message.type == Gst.MessageType.EOS:
                            raise RuntimeError("Unexpected end of media stream")
                    if self.last_state[0] == "starting":
                        if time.monotonic() - self.last_probe > 0.5:
                            self.last_probe = time.monotonic()
                            if self.stream_ready():
                                self.state("streaming", self.url)
                        if (
                            time.monotonic() - self.started_at > 15
                            and self.last_state[0] == "starting"
                        ):
                            raise RuntimeError(
                                "RTSP publishing timed out after 15 seconds"
                            )
                except Exception as error:
                    self.state("stream_error", str(error))
                    self.stop_pipeline()
                    self.stop_server()
                    self.formats.clear()
                    self.last_input.clear()
                    self.stop.wait(2)
        finally:
            self.stop_pipeline()
            self.stop_server()

    def close(self):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=10)
            if self.thread.is_alive():
                raise RuntimeError("Streaming worker did not shut down")
        self.directory.cleanup()
