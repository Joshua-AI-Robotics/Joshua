import asyncio
import base64
import hashlib
import io
import json
import re
import subprocess
import time
from collections import deque

from google.protobuf import text_format
from PIL import Image, ImageDraw

from ai.inference.manifest import repo_root
from ai.inference.motor_policy import (
    EvidenceSample,
    MeasuredPosition,
    NoCommand,
    PolicyRequest,
    RequestId,
    Rgb8,
    SampleId,
)
from ai.models.registry import create_adapter
from config.proto.config_pb2 import Config

HEAD_REQUEST = re.compile(
    r"(?:please )?(?:look at|point (?:the|your) head at|turn (?:the|your) head toward) "
    r"(?:(?:the|a|an) )?([a-z]+(?:[ -][a-z]+){0,5})[.!]?",
    re.IGNORECASE | re.ASCII,
)


NO_MOTION = re.compile(
    r"\b(?:do not|don['’]t|never|without|no|stop)\s+(?:(?:the|your|head)\s+)*"
    r"(?:mov(?:e|ing|ement)|turn(?:ing)?|pan(?:ning)?|motion|"
    r"look(?:ing)?|point(?:ing)?|track(?:ing)?)\b",
    re.IGNORECASE,
)


def target_words(value, text):
    if not isinstance(value, str) or not re.fullmatch(
        r"[\w]+(?:[ -][\w]+){0,5}", value
    ):
        return None
    target = value.casefold()
    if not re.search(r"(?<!\w)" + re.escape(target) + r"(?!\w)", text.casefold()):
        return None
    if set(target.replace("-", " ").split()) & {
        "it",
        "this",
        "that",
        "them",
        "one",
        "there",
        "and",
        "or",
        "but",
        "without",
        "never",
        "no",
        "not",
        "if",
        "unless",
        "before",
        "after",
        "instead",
    } or target in {"left", "right", "me", "him", "her"}:
        return None
    return target


class HeadReplay:
    def __init__(self, adapter, config_id, question, joint, frame, measured_position):
        self.adapter = adapter
        self.config_id = config_id
        self.question = question
        self.joint = joint
        self.frame = Rgb8(*frame.size, frame.tobytes())
        encoded = io.BytesIO()
        frame.save(encoded, format="PNG")
        self.image_url = (
            "data:image/png;base64," + base64.b64encode(encoded.getvalue()).decode()
        )
        self.measured_position = measured_position
        self.commands = deque(maxlen=64)
        self.busy = False

    @classmethod
    def load(cls, path=None):
        root = repo_root()
        path = path or root / "config/config_preset/example/decisions_head_replay.pbtxt"
        subprocess.run(
            [root / "config/joshua_validate_config", "--config", path],
            check=True,
            capture_output=True,
        )
        data = path.read_bytes()
        config = text_format.Parse(data.decode(), Config())
        (model,) = config.ai.models.single_models
        policy = model.decisions_config.policy
        (question,) = policy.questions
        (joint,) = policy.requirements.joints
        frame = Image.new("RGB", (320, 180), "#ede8dd")
        draw = ImageDraw.Draw(frame)
        draw.rectangle((0, 122, 319, 179), fill="#bfa785")
        for x, color in ((35, "#254da5"), (224, "#cf332c")):
            draw.ellipse((x + 36, 69, x + 70, 108), outline=color, width=8)
            draw.rounded_rectangle((x, 60, x + 50, 125), radius=9, fill=color)
            draw.ellipse((x, 55, x + 50, 69), fill=color)
            draw.ellipse((x + 4, 58, x + 46, 65), fill="#392c28")
        return cls(
            create_adapter(model, config),
            hashlib.sha256(data).hexdigest(),
            question,
            joint,
            frame,
            0.0,
        )

    @staticmethod
    def motion_target(text):
        explicit = HEAD_REQUEST.fullmatch(text)
        target = target_words(explicit[1], text) if explicit else None
        return not NO_MOTION.search(text) and (bool(target) or not explicit), target

    @staticmethod
    async def extract_target(text, trace, api):
        started = time.monotonic()
        binding = await api.responses.create(
            model="gpt-4.1-mini-2025-04-14",
            store=False,
            max_output_tokens=96,
            instructions=(
                "Extract one named visual target from this turn. Copy its exact words. "
                "Return null unless the user explicitly requests an unconditional "
                "head movement. Return null for questions about capabilities, "
                "prohibitions, directions without an object or unclear referents."
            ),
            input=text,
            text={
                "format": {
                    "type": "json_schema",
                    "name": "head_target",
                    "strict": True,
                    "schema": {
                        "type": "object",
                        "additionalProperties": False,
                        "required": ["target"],
                        "properties": {"target": {"type": ["string", "null"]}},
                    },
                }
            },
        )
        trace["binding_ms"] = round((time.monotonic() - started) * 1000)
        trace["binding_id"] = binding._request_id
        trace["binding_tokens"] = getattr(binding.usage, "total_tokens", None)
        if binding.status == "completed" and binding.output_text:
            bound = json.loads(binding.output_text)
            return target_words(bound.get("target"), text)
        return None

    async def step(self, target, trace, api, admissible):
        if self.busy:
            trace["outcome"] = "busy"
            return "The replay head is busy with another turn."
        self.busy = True
        started = time.monotonic_ns()
        result = asyncio.get_running_loop().create_future()

        def record(event):
            if event.phase == "result" and not result.done():
                result.set_result(event.evaluation)

        def sink(command):
            if admissible():
                self.commands.append(command)
                trace["replay_command"] = {
                    "joint": command.outcome.joint_name,
                    "radians": command.outcome.position,
                }

        try:
            async with self.adapter.session(
                run=trace["turn_id"],
                config_id=self.config_id,
                client=api,
                record=record,
                sink=sink,
            ) as session:
                generation = session.start(
                    trigger="frame",
                    sources={"frame": ("replay_camera", 1), "target": ("operator", 1)},
                    feedback=("replay_encoder", 1),
                )
                now = time.monotonic_ns()
                seq = trace["sequence"]
                request = PolicyRequest(
                    1,
                    RequestId(trace["turn_id"], self.config_id, generation, seq),
                    self.question.id,
                    (
                        EvidenceSample(
                            "frame", SampleId("replay_camera", 1, seq), self.frame, now
                        ),
                        EvidenceSample(
                            "target", SampleId("operator", 1, seq), target, now
                        ),
                    ),
                    MeasuredPosition(
                        self.joint.name,
                        SampleId("replay_encoder", 1, seq),
                        self.measured_position,
                        self.joint.unit,
                        now,
                    ),
                )
                session.submit(request)
                evaluation = await result
                trace["motor"] = evaluation.details
                if "replay_command" in trace:
                    return None
                if not admissible():
                    raise asyncio.CancelledError()
                reason = evaluation.result.outcome
                trace["outcome"] = (
                    reason.value if isinstance(reason, NoCommand) else "cancelled"
                )
                if reason is NoCommand.LOW_CONFIDENCE:
                    return (
                        "Replay issued no command. The visual decision was uncertain."
                    )
                return (
                    "Replay issued no command ("
                    + str(evaluation.details.get("choice", trace["outcome"]))
                    + ")."
                )
        finally:
            trace["motor_ms"] = (time.monotonic_ns() - started) // 1_000_000
            self.busy = False
