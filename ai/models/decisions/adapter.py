from __future__ import annotations

import asyncio
import base64
import copy
import io
import json
from contextlib import asynccontextmanager
from functools import partial

from openai import (
    APIConnectionError,
    APIStatusError,
    APITimeoutError,
    AsyncOpenAI,
    RateLimitError,
)
from PIL import Image

from ai.inference.adapter import InferenceAdapter
from ai.inference.motor_policy import (
    ChoiceAnswer,
    NoCommand,
    PolicyResult,
    bind_question,
    map_answer,
    sample_ids,
)
from ai.inference.policy_session import Evaluation, PolicySession
from ai.inference.proto.motor_policy_pb2 import MotorPolicy
from ai.inference.types import AdapterSpec
from ai.models.decisions.choice import decode_choice


def _request_body(settings, request):
    policy = settings.policy
    question = bind_question(policy, request)
    if isinstance(question, NoCommand):
        return question, None
    kinds = {item.name: item.kind for item in policy.requirements.evidence}
    samples = {s.name: s for s in request.evidence}
    content = []
    for name in question.inputs:
        value = samples[name].payload
        content.append({"type": "input_text", "text": name})
        if kinds[name] == MotorPolicy.Evidence.RGB8:
            buffer = io.BytesIO()
            with Image.frombytes(
                "RGB", (value.width, value.height), value.pixels
            ) as image:
                image.save(buffer, format="PNG")
            encoded = base64.b64encode(buffer.getvalue()).decode("ascii")
            content.append(
                {"type": "input_image", "image_url": "data:image/png;base64," + encoded}
            )
        elif kinds[name] == MotorPolicy.Evidence.JSON:
            data = json.loads(value)
            if not (
                isinstance(data, dict)
                or isinstance(data, list)
                and all(isinstance(v, dict) for v in data)
            ):
                return NoCommand.INVALID_EVIDENCE, None
            content.append({"type": "input_json", "data": data})
        else:
            content.append({"type": "input_text", "text": value})
    if request.measured is not None:
        measured = request.measured
        content.append(
            {
                "type": "input_json",
                "data": {
                    "measured_position": {
                        "joint": measured.joint_name,
                        "value": measured.value,
                        "unit": MotorPolicy.Joint.Unit.Name(measured.unit),
                    }
                },
            }
        )
    return question, {
        "model": settings.model,
        "input": [{"role": "user", "content": content}],
        "questions": [
            {
                "type": "choice",
                "name": question.id,
                "instructions": question.prompt,
                "choices": [{"value": c.label} for c in question.choices],
            }
        ],
    }


async def _evaluate(settings, api, request):
    details = {"model": settings.model}

    def finish(outcome):
        return Evaluation(
            PolicyResult(request.id, sample_ids(request), outcome), details
        )

    encoding = asyncio.create_task(asyncio.to_thread(_request_body, settings, request))
    try:
        question, body = await asyncio.shield(encoding)
    except asyncio.CancelledError:
        await encoding
        raise
    if body is None:
        return finish(question)
    try:
        raw = await api.decisions.with_raw_response.create(**body)
    except APITimeoutError:
        return finish(NoCommand.EXPIRED)
    except RateLimitError as exc:
        details["http_status"] = exc.status_code
        return finish(NoCommand.RATE_LIMITED)
    except APIStatusError as exc:
        details["http_status"] = exc.status_code
        return finish(NoCommand.PROVIDER_FAILED)
    except APIConnectionError:
        return finish(NoCommand.PROVIDER_FAILED)
    details["api_request_id"] = raw.headers.get("x-request-id")
    try:
        response = raw.http_response.json()
    except (ValueError, RecursionError):
        return finish(NoCommand.MALFORMED_RESPONSE)
    if not isinstance(response, dict) or response.get("model") != settings.model:
        return finish(NoCommand.MALFORMED_RESPONSE)
    answers = response.get("answers")
    if not isinstance(answers, list) or len(answers) != 1:
        return finish(NoCommand.MALFORMED_RESPONSE)
    usage = response.get("usage")
    if isinstance(usage, dict):
        details["tokens"] = {
            k: usage[k]
            for k in ("input_tokens", "output_tokens", "total_tokens")
            if type(usage.get(k)) is int
        }
    answer = answers[0]
    try:
        choice = decode_choice(
            question.id,
            (c.label for c in question.choices),
            answer,
            settings.min_confidence,
        )
    except ValueError as exc:
        choice = NoCommand(str(exc))
    if not isinstance(choice, NoCommand) or choice is NoCommand.LOW_CONFIDENCE:
        details.update(choice=answer["choice"], confidence=answer["confidence"])
    if isinstance(choice, NoCommand):
        return finish(choice)
    return finish(
        map_answer(
            settings.policy,
            request,
            question,
            ChoiceAnswer(request.id, question.id, choice),
        )
    )


class DecisionsAdapter(InferenceAdapter):
    def spec(self):
        return AdapterSpec(
            motor_policy=self._single_model.decisions_config.policy.requirements
        )

    def on_observation(self, observation):
        raise ValueError("Decisions requires a named motor-policy session")

    @asynccontextmanager
    async def session(self, *, run, config_id, record, sink, client=None):
        settings = copy.deepcopy(self._single_model.decisions_config)
        options = dict(
            max_retries=settings.max_attempts - 1, timeout=settings.deadline_ms / 1000
        )
        api = (
            AsyncOpenAI(**options) if client is None else client.with_options(**options)
        )

        session = PolicySession(
            run=run,
            config=config_id,
            deadline_ms=settings.deadline_ms,
            max_receipt_age_ms=settings.max_receipt_age_ms,
            evaluate=partial(_evaluate, settings, api),
            record=record,
            sink=sink,
        )
        try:
            yield session
        finally:
            try:
                await session.close()
            finally:
                if client is None:
                    await api.close()
