from __future__ import annotations

import json
import math
from dataclasses import dataclass
from enum import Enum
from typing import Callable

from robot.action.proto.action_packet_pb2 import JointCommand

from ai.inference.proto.motor_policy_pb2 import MotorPolicy


@dataclass(frozen=True)
class RequestId:
    run: str
    config: str
    generation: int
    sequence: int


@dataclass(frozen=True)
class SampleId:
    source: str
    generation: int
    sequence: int


@dataclass(frozen=True)
class Rgb8:
    width: int
    height: int
    pixels: bytes


@dataclass(frozen=True)
class EvidenceSample:
    name: str
    id: SampleId
    payload: str | Rgb8


@dataclass(frozen=True)
class MeasuredPosition:
    joint_name: str
    id: SampleId
    value: float
    unit: int


@dataclass(frozen=True)
class PolicyRequest:
    version: int
    id: RequestId
    question_id: str
    evidence: tuple[EvidenceSample, ...]
    measured: MeasuredPosition | None = None


@dataclass(frozen=True)
class ChoiceAnswer:
    request_id: RequestId
    question_id: str
    label: str


class NoCommand(str, Enum):
    INCOMPATIBLE_VERSION = "incompatible_version"
    INVALID_REQUEST_IDENTITY = "invalid_request_identity"
    INVALID_SAMPLE_IDENTITY = "invalid_sample_identity"
    UNKNOWN_QUESTION = "unknown_question"
    INVALID_INPUTS = "invalid_inputs"
    INVALID_EVIDENCE = "invalid_evidence"
    INVALID_MEASURED_POSITION = "invalid_measured_position"
    MISSING_MEASURED_POSITION = "missing_measured_position"
    MISMATCHED_ANSWER = "mismatched_answer"
    UNKNOWN_CHOICE = "unknown_choice"
    NO_COMMAND = "no_command"
    TARGET_OUT_OF_BOUNDS = "target_out_of_bounds"


@dataclass(frozen=True)
class PolicyResult:
    request_id: RequestId
    samples: tuple[SampleId, ...]
    outcome: JointCommand | NoCommand


def _counter(value: int) -> bool:
    return type(value) is int and value >= 0


def _valid_sample_id(sample: SampleId) -> bool:
    return (
        bool(sample.source)
        and _counter(sample.generation)
        and _counter(sample.sequence)
    )


def _finite_json_number(token: str) -> float:
    value = float(token)
    if not math.isfinite(value):
        raise ValueError("nonfinite JSON number")
    return value


def _valid_payload(kind: int, payload: str | Rgb8) -> bool:
    if kind == MotorPolicy.Evidence.RGB8:
        return (
            isinstance(payload, Rgb8)
            and type(payload.width) is int
            and type(payload.height) is int
            and payload.width > 0
            and payload.height > 0
            and isinstance(payload.pixels, bytes)
            and len(payload.pixels) == payload.width * payload.height * 3
        )
    if not isinstance(payload, str) or not payload.strip():
        return False
    if kind == MotorPolicy.Evidence.TEXT:
        return True
    if kind != MotorPolicy.Evidence.JSON:
        return False
    try:
        json.loads(
            payload, parse_float=_finite_json_number, parse_constant=_finite_json_number
        )
    except (ValueError, RecursionError):
        return False
    return True


def evaluate_choice(
    policy: MotorPolicy,
    request: PolicyRequest,
    provider: Callable[[MotorPolicy.Question, PolicyRequest], ChoiceAnswer],
) -> PolicyResult:
    """Evaluate one fixed-choice request against a policy accepted by ValidateConfig."""
    samples = tuple(sample.id for sample in request.evidence)
    measured = request.measured
    if measured is not None:
        samples += (measured.id,)

    def result(outcome: JointCommand | NoCommand) -> PolicyResult:
        return PolicyResult(request.id, samples, outcome)

    if (
        type(request.version) is not int
        or request.version != 1
        or policy.requirements.version != request.version
    ):
        return result(NoCommand.INCOMPATIBLE_VERSION)
    identity = request.id
    if (
        not identity.run
        or not identity.config
        or not _counter(identity.generation)
        or not _counter(identity.sequence)
    ):
        return result(NoCommand.INVALID_REQUEST_IDENTITY)
    if not all(_valid_sample_id(sample) for sample in samples):
        return result(NoCommand.INVALID_SAMPLE_IDENTITY)

    question = next((q for q in policy.questions if q.id == request.question_id), None)
    if question is None:
        return result(NoCommand.UNKNOWN_QUESTION)
    kinds = {source.name: source.kind for source in policy.requirements.evidence}
    names = [sample.name for sample in request.evidence]
    if len(names) != len(question.inputs) or set(names) != set(question.inputs):
        return result(NoCommand.INVALID_INPUTS)
    if not all(_valid_payload(kinds[s.name], s.payload) for s in request.evidence):
        return result(NoCommand.INVALID_EVIDENCE)

    joint = next(j for j in policy.requirements.joints if j.name == question.joint_name)
    if measured is not None:
        if (
            measured.joint_name != joint.name
            or measured.unit != joint.unit
            or type(measured.value) not in (int, float)
            or not joint.minimum <= measured.value <= joint.maximum
        ):
            return result(NoCommand.INVALID_MEASURED_POSITION)
    if measured is None and any(c.HasField("position_delta") for c in question.choices):
        return result(NoCommand.MISSING_MEASURED_POSITION)

    answer = provider(question, request)
    if (
        not isinstance(answer, ChoiceAnswer)
        or answer.request_id != identity
        or answer.question_id != question.id
    ):
        return result(NoCommand.MISMATCHED_ANSWER)
    choice = next((c for c in question.choices if c.label == answer.label), None)
    if choice is None:
        return result(NoCommand.UNKNOWN_CHOICE)
    if choice.HasField("no_command"):
        return result(NoCommand.NO_COMMAND)
    if choice.HasField("position_delta"):
        target = measured.value + choice.position_delta
    else:
        target = choice.position
    if not joint.minimum <= target <= joint.maximum:
        return result(NoCommand.TARGET_OUT_OF_BOUNDS)
    return result(
        JointCommand(
            joint_name=joint.name,
            position=target,
            position_encoding=JointCommand.POSITION_SI,
        )
    )
