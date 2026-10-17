"""Bounded, ROS-independent event policy and local Home Assistant delivery."""

import hashlib
import ipaddress
import json
import math
import os
import re
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from collections import OrderedDict, deque
from pathlib import Path

RECOGNIZED = {
    "audio_classification",
    "cry_started",
    "cry_ended",
    "video_detection",
    "input_stale",
    "input_restored",
    "inference_error",
    "stream_error",
    "waiting_for_input",
    "starting",
    "streaming",
}
ERRORS = {"input_stale", "inference_error", "stream_error"}


def validate_options(options):
    url = urllib.parse.urlsplit(options.home_assistant_url)
    local = False
    try:
        local = ipaddress.ip_address(url.hostname or "").is_loopback
    except ValueError:
        pass
    if (
        url.scheme not in ("http", "https")
        or not url.hostname
        or (url.scheme == "http" and not local)
        or url.username
        or url.password
        or url.path not in ("", "/")
        or url.query
        or url.fragment
    ):
        raise ValueError("Home Assistant needs HTTPS, or HTTP on a loopback IP")
    if (
        not re.fullmatch(r"[A-Z][A-Z0-9_]*", options.webhook_file_env)
        or not 1 <= options.request_timeout_sec <= 10
        or not 5 <= options.max_event_age_sec <= 120
        or not 1 <= options.cooldown_sec <= 3600
        or not 0.5 <= options.movement_persistence_sec <= 30
        or not 2 <= options.input_timeout_sec <= 60
    ):
        raise ValueError("Invalid notification limits or secret environment name")


class EventPolicy:
    """State transitions and freshness checks, called only on the ROS thread."""

    def __init__(self, options, topics, wall=time.time_ns, clock=time.monotonic):
        self.options, self.wall, self.clock = options, wall, clock
        self.inputs = {topic: "waiting" for topic in topics}
        self.seen_at = {}
        self.roles = {}
        self.started = clock()
        self.ids = OrderedDict()
        self.alerted = {}
        self.crying = None
        self.moving = None
        self.motion_since = None
        self.motion_alerted = False
        self.last_event = "Waiting for inference and media events"

    def envelope(self, event_id=None, kind="", message="", notify=False, stamp=None):
        now = self.wall()
        return {
            "schema_version": 1,
            "event_id": event_id or str(uuid.uuid4()),
            "observed_at_ns": now if stamp is None else stamp,
            "expires_at_ns": (now if stamp is None else stamp)
            + int(self.options.max_event_age_sec * 1e9),
            "kind": kind,
            "title": "Baby Monitor",
            "message": message,
            "notify": notify,
            "crying": self.crying,
            "moving": self.moving,
            "inputs": dict(self.inputs),
            "last_event": self.last_event,
        }

    def alert(self, key):
        now = self.clock()
        if now - self.alerted.get(key, -math.inf) < self.options.cooldown_sec:
            return False
        self.alerted[key] = now
        return True

    def receive(self, topic, raw):
        if topic not in self.inputs or len(raw) > 65536:
            raise ValueError("Unknown topic or oversized event")
        event = json.loads(raw)
        if not isinstance(event, dict) or event.get("schema_version") != 1:
            raise ValueError("Unsupported event schema")
        kind = event.get("event_type")
        if kind not in RECOGNIZED:
            raise ValueError("Unknown event type")
        stamp = event.get("observed_at_ns")
        if (
            type(stamp) is not int
            or not -1e9 <= self.wall() - stamp <= self.options.max_event_age_sec * 1e9
        ):
            raise ValueError("Stale event or incompatible clock")
        event_id = event.get("event_id") or hashlib.sha256(raw.encode()).hexdigest()
        if not isinstance(event_id, str) or not 1 <= len(event_id) <= 128:
            raise ValueError("Invalid event ID")
        if event_id in self.ids:
            return None
        # Validate data before it changes availability or duplicate tracking.
        if kind == "audio_classification" and type(event.get("crying")) is not bool:
            raise ValueError("Audio classification requires a crying boolean")
        if kind == "video_detection" and type(event.get("movement")) is not bool:
            raise ValueError("Video detection requires a movement boolean")
        now = self.clock()
        if kind == "video_detection" and now - self.seen_at.get(topic, now) > 2:
            self.motion_since, self.motion_alerted = None, False
        self.ids[event_id] = None
        if len(self.ids) > 512:
            self.ids.popitem(last=False)
        previous = self.inputs[topic]
        self.seen_at[topic] = now
        self.inputs[topic] = (
            kind
            if kind in ERRORS or kind in ("starting", "waiting_for_input")
            else "ok"
        )
        notify, message, alert_kind = False, "", kind
        if kind in ("audio_classification", "cry_started", "cry_ended"):
            self.roles[topic] = "audio"
            self.crying = event.get("crying", kind == "cry_started")
            if kind == "cry_started":
                message = "Crying detected. Open the camera to check."
                notify = self.alert(("cry", topic))
        elif kind == "video_detection":
            self.roles[topic] = "video"
            self.moving = event["movement"]
            if self.moving:
                if self.motion_since is None:
                    self.motion_since = now
                if (
                    now - self.motion_since >= self.options.movement_persistence_sec
                    and not self.motion_alerted
                ):
                    self.motion_alerted = True
                    alert_kind = "movement"
                    message = "Sustained movement detected. Open the camera to check."
                    notify = self.alert(("movement", topic))
            else:
                self.motion_since, self.motion_alerted = None, False
        elif kind in ERRORS:
            self.invalidate(topic)
            message = f"Monitor input needs attention: {topic} ({kind})."
            notify = previous != self.inputs[topic] and self.alert(("error", topic))
        if previous in ERRORS or previous == "offline":
            if self.inputs[topic] == "ok" and not message:
                message = f"Monitor input restored: {topic}."
                alert_kind = "input_restored"
                notify = self.alert(("restored", topic))
        if message:
            self.last_event = message
        return self.envelope(event_id, alert_kind, message, notify, stamp)

    def invalidate(self, topic):
        if self.roles.get(topic) == "audio":
            self.crying = None
        if self.roles.get(topic) == "video":
            self.moving = None
            self.motion_since, self.motion_alerted = None, False

    def tick(self):
        alerts = []
        for topic in self.inputs:
            if (
                self.clock() - self.seen_at.get(topic, self.started)
                > self.options.input_timeout_sec
            ):
                if self.inputs[topic] != "offline":
                    self.inputs[topic] = "offline"
                    self.invalidate(topic)
                    self.last_event = f"No recent events from {topic}."
                    alerts.append(
                        self.envelope(
                            kind="input_stale",
                            message=self.last_event,
                            notify=self.alert(("error", topic)),
                        )
                    )
        return alerts


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class HomeAssistantSink:
    """HTTP worker: bounded alert FIFO plus one coalesced status, no ROS blocking."""

    def __init__(self, options, report, wall=time.time_ns):
        validate_options(options)
        self.options, self.report, self.wall = options, report, wall
        self.pending = deque(maxlen=32)
        self.latest = None
        self.condition = threading.Condition()
        self.stopped = False
        self.thread = None
        # Avoid sending local webhook credentials through environment HTTP proxies.
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({}), NoRedirect()
        )

    def submit(self, payload):
        with self.condition:
            if payload["notify"]:
                if len(self.pending) == self.pending.maxlen:
                    self.report("queue_full", "Dropped oldest notification")
                self.pending.append(payload)
            else:
                self.latest = payload
            self.condition.notify()

    def send(self, payload):
        path = os.environ.get(self.options.webhook_file_env)
        if not path:
            raise ValueError("Webhook credential file environment variable is unset")
        secret = Path(path).read_text().strip()
        if not re.fullmatch("[a-f0-9]{64}", secret):
            raise ValueError(
                "Webhook credential must contain 64 hexadecimal characters"
            )
        request = urllib.request.Request(
            self.options.home_assistant_url.rstrip("/") + "/api/webhook/" + secret,
            data=json.dumps(payload, allow_nan=False).encode(),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with self.opener.open(
            request, timeout=self.options.request_timeout_sec
        ) as response:
            if not 200 <= response.status < 300:
                raise ValueError("Home Assistant rejected the request")
            response.read(4096)

    def start(self):
        self.thread = threading.Thread(
            target=self.run, name="home-assistant-delivery", daemon=True
        )
        self.thread.start()

    def run(self):
        while True:
            with self.condition:
                self.condition.wait_for(
                    lambda: self.stopped or self.pending or self.latest
                )
                if self.stopped:
                    return
                payload = self.pending.popleft() if self.pending else self.latest
                if payload is self.latest:
                    self.latest = None
            delay = 1
            while self.wall() < payload["expires_at_ns"]:
                try:
                    self.send(payload)
                except Exception as error:
                    # URLs include the webhook secret: never log exception text.
                    self.report("delivery_error", type(error).__name__)
                    with self.condition:
                        self.condition.wait_for(lambda: self.stopped, timeout=delay)
                        if self.stopped:
                            return
                    delay = min(delay * 2, 10)
                    # Status must reflect the newest state after an outage.
                    if not payload["notify"]:
                        with self.condition:
                            if self.latest:
                                payload, self.latest = self.latest, None
                else:
                    self.report("accepted", payload["event_id"])
                    break
            else:
                self.report("expired", payload["event_id"])

    def close(self):
        with self.condition:
            self.stopped = True
            self.condition.notify_all()
        if self.thread:
            self.thread.join(self.options.request_timeout_sec + 2)
