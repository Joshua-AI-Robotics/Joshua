"""No hardware: bounded queues, private listeners, timestamps, and real codecs."""

import json
import math
import socket
import struct
import sys
import threading
import time
import unittest
import urllib.request
from pathlib import Path

from config.proto import config_pb2
from ros2.utils.media_stream import (
    MediaStream,
    PendingSamples,
    Sample,
    private_address,
    server_config,
    validate_options,
)


def options():
    c = config_pb2.Config().utilities.media_streams.add()
    c.width, c.height, c.fps, c.bitrate_kbps = 160, 120, 10, 300
    c.stream_name = "test"
    c.listen_address = "127.0.0.1"
    sockets = [socket.socket() for _ in range(3)]
    try:
        for sock in sockets:
            sock.bind(("127.0.0.1", 0))
        c.http_port, c.ice_port, c.rtsp_port = [s.getsockname()[1] for s in sockets]
    finally:
        for sock in sockets:
            sock.close()
    return c


class MediaTest(unittest.TestCase):
    def test_private_config(self):
        c = options()
        validate_options(c)
        for address in ("0.0.0.0", "192.168.1.2", "8.8.8.8", "::"):
            with self.assertRaises(ValueError):
                private_address(address)
        self.assertEqual(private_address("100.64.0.10"), "100.64.0.10")
        config = server_config(c, "100.64.0.10")
        self.assertEqual(config["rtspAddress"], f"127.0.0.1:{c.rtsp_port}")
        self.assertFalse(config["webrtcIPsFromInterfaces"])
        self.assertEqual(config["webrtcAdditionalHosts"], ["100.64.0.10"])
        self.assertEqual(config["authInternalUsers"][0]["ips"], ["127.0.0.0/8"])
        c.stream_name = "bad/name ! fakesink"
        with self.assertRaises(ValueError):
            validate_options(c)

    def test_backpressure_keeps_recent_samples(self):
        queue = PendingSamples()
        for i in range(100):
            queue.put(Sample("video", i, b"v", "", 100_000_000))
            queue.put(Sample("audio", i, b"a", "", 20_000_000))
        samples = queue.take()
        self.assertEqual([s.stamp_ns for s in samples if s.kind == "video"], [99])
        audio = [s for s in samples if s.kind == "audio"]
        self.assertEqual(len(audio), 10)
        self.assertTrue(audio[0].discontinuity)
        self.assertEqual(queue.dropped_audio, 90)
        self.assertEqual(queue.dropped_video, 99)

    def test_real_stream_and_recovery(self):
        # GI is supplied by Ubuntu, matching the selected Python minor version.
        sys.path.append("/usr/lib/python3/dist-packages")
        import gi

        gi.require_version("Gst", "1.0")
        from gi.repository import Gst

        Gst.init(None)
        c = options()
        events = []
        stream = MediaStream(c, True, lambda *event: events.append(event))
        stop = threading.Event()
        paused = threading.Event()

        def produce():
            sequence = 0
            while not stop.wait(0.02):
                if paused.is_set():
                    continue
                now = time.time_ns()
                audio = struct.pack(
                    "<320h",
                    *[
                        int(
                            4000
                            * math.sin(2 * math.pi * 440 * (sequence * 320 + i) / 16000)
                        )
                        for i in range(320)
                    ],
                )
                stream.submit(
                    Sample(
                        "audio",
                        now,
                        audio,
                        "audio/x-raw,format=S16LE,layout=interleaved,"
                        "rate=16000,channels=1",
                        20_000_000,
                    )
                )
                if sequence % 5 == 0:
                    stream.submit(
                        Sample(
                            "video",
                            now,
                            bytes([sequence % 255, 60, 100]) * (160 * 120),
                            "video/x-raw,format=RGB,width=160,height=120,"
                            "framerate=10/1",
                            100_000_000,
                        )
                    )
                sequence += 1

        def wait_for(predicate, timeout=15):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if predicate():
                    return
                time.sleep(0.1)
            errors = []
            if receiver:
                bus = receiver.get_bus()
                while True:
                    msg = bus.pop_filtered(Gst.MessageType.ERROR)
                    if not msg:
                        break
                    errors.append(str(msg.parse_error()))
            self.fail(f"Timed out; events={events}, receiver={errors}")

        receiver = None
        producer = threading.Thread(target=produce)
        try:
            stream.start()
            producer.start()
            wait_for(lambda: any(e[0] == "streaming" for e in events))
            with urllib.request.urlopen(stream.url, timeout=5) as response:
                self.assertEqual(response.status, 200)
                self.assertIn(b"video", response.read())
            # Negotiate real RTSP tracks and decode both payloads, not merely
            # assert that the pipeline process is alive.
            receiver = Gst.parse_launch(
                f"rtspsrc location=rtsp://127.0.0.1:{c.rtsp_port}/test "
                "protocols=tcp latency=50 name=s "
                "s. ! application/x-rtp,media=video ! rtph264depay ! "
                "h264parse ! avdec_h264 ! "
                "fakesink name=v sync=false signal-handoffs=true "
                "s. ! application/x-rtp,media=audio ! rtpopusdepay ! opusdec ! "
                "fakesink name=a sync=false signal-handoffs=true"
            )
            counts = {"a": 0, "v": 0}
            timestamps = {"a": [], "v": []}

            def received(sink, buffer, pad, kind):
                counts[kind] += 1
                timestamps[kind].append(buffer.pts)

            for kind in counts:
                receiver.get_by_name(kind).connect("handoff", received, kind)
            receiver.set_state(Gst.State.PLAYING)
            wait_for(lambda: counts["v"] >= 15 and counts["a"] >= 30)
            for stamps in timestamps.values():
                self.assertEqual(stamps, sorted(stamps))
            self.assertLess(abs(timestamps["a"][-1] - timestamps["v"][-1]), 500_000_000)
            receiver.set_state(Gst.State.NULL)
            receiver = None
            paused.set()
            wait_for(lambda: any(e[0] == "input_stale" for e in events), 7)
            prior = sum(e[0] == "streaming" for e in events)
            paused.clear()
            wait_for(lambda: sum(e[0] == "streaming" for e in events) > prior)
            # A crashed media server must be reaped and restarted.
            previous = stream.server.pid
            stream.server.kill()
            wait_for(
                lambda: stream.server is not None and stream.server.pid != previous
            )
            wait_for(lambda: sum(e[0] == "streaming" for e in events) > prior + 1)
        finally:
            stop.set()
            if producer.is_alive():
                producer.join(2)
            if receiver:
                receiver.set_state(Gst.State.NULL)
            stream.close()
        self.assertIsNone(stream.server)
        self.assertIsNone(stream.pipeline)
        self.assertFalse(Path(stream.directory.name).exists())
        print(json.dumps({"decoded": counts, "events": events}))


if __name__ == "__main__":
    unittest.main()
