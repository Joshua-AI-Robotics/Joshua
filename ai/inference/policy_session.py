from __future__ import annotations

import asyncio
import copy
import time
from dataclasses import dataclass, replace
from typing import Awaitable, Callable, Mapping

from ai.inference.motor_policy import (
    MeasuredPosition,
    NoCommand,
    PolicyRequest,
    PolicyResult,
    RequestId,
    SampleId,
    sample_ids,
)


@dataclass(frozen=True)
class Evaluation:
    result: PolicyResult
    details: dict


@dataclass(frozen=True)
class PolicyEvent:
    phase: str
    request_id: RequestId
    question_id: str
    samples: tuple[SampleId, ...]
    monotonic_ns: int
    expires_ns: int
    evaluation: Evaluation | None


@dataclass
class _Submission:
    request: PolicyRequest
    expires_ns: int
    timer: asyncio.TimerHandle | None = None


class PolicySession:
    """Use on one event loop with synchronous, nonblocking callbacks."""

    def __init__(
        self,
        *,
        run: str,
        config: str,
        deadline_ms: int,
        max_receipt_age_ms: int,
        evaluate: Callable[[PolicyRequest], Awaitable[Evaluation]],
        record: Callable[[PolicyEvent], None],
        sink: Callable[[PolicyResult], None],
        clock=time.monotonic_ns,
    ):
        self._loop = asyncio.get_running_loop()
        self._clock = clock
        self._run = run
        self._config = config
        self._deadline_ns = deadline_ms * 1_000_000
        self._age_ns = max_receipt_age_ms * 1_000_000
        self._evaluate = evaluate
        self._record = record
        self._sink = sink
        self._generation = 0
        self._active = False
        self._closed = False
        self._task = None
        self._current = None
        self._pending = None

    def _check_loop(self):
        if asyncio.get_running_loop() is not self._loop:
            raise RuntimeError("PolicySession belongs to another event loop")

    def start(
        self,
        *,
        trigger: str,
        sources: Mapping[str, tuple[str, int]],
        feedback: tuple[str, int] | None = None,
    ) -> int:
        self._check_loop()
        if self._closed or self._active or trigger not in sources:
            raise ValueError("Start requires an inactive session and a bound trigger")
        self._generation += 1
        self._active = True
        self._trigger = trigger
        self._sources = dict(sources)
        self._feedback = feedback
        self._last_request = -1
        self._last_sample = {}
        self._started_ns = self._clock()
        return self._generation

    def _event(self, phase, item, evaluation=None):
        req = item.request
        self._record(
            PolicyEvent(
                phase,
                req.id,
                req.question_id,
                sample_ids(req),
                self._clock(),
                item.expires_ns,
                copy.deepcopy(evaluation),
            )
        )

    def _reject(self, item, reason, details=None):
        if item.timer:
            item.timer.cancel()
        req = item.request
        self._event(
            "result",
            item,
            Evaluation(PolicyResult(req.id, sample_ids(req), reason), details or {}),
        )
        return reason

    def submit(self, request: PolicyRequest) -> NoCommand | None:
        self._check_loop()
        now = self._clock()
        deadline = now + self._deadline_ns
        item = _Submission(request, deadline)
        if not self._active:
            return self._reject(item, NoCommand.INACTIVE)
        identity = request.id
        if (identity.run, identity.config, identity.generation) != (
            self._run,
            self._config,
            self._generation,
        ):
            return self._reject(item, NoCommand.WRONG_SESSION)
        if (
            type(identity.sequence) is not int
            or identity.sequence <= self._last_request
        ):
            return self._reject(item, NoCommand.DUPLICATE)

        sequences = {}
        if type(request.evidence) is not tuple:
            return self._reject(item, NoCommand.INVALID_INPUTS)
        samples = (
            (*request.evidence, request.measured)
            if request.measured
            else request.evidence
        )
        for sample in samples:
            is_feedback = isinstance(sample, MeasuredPosition)
            key = (is_feedback, "" if is_feedback else sample.name)
            expected = self._feedback if is_feedback else self._sources.get(sample.name)
            if expected != (sample.id.source, sample.id.generation):
                return self._reject(item, NoCommand.WRONG_SOURCE)
            if key in sequences:
                return self._reject(item, NoCommand.INVALID_INPUTS)
            seq = sample.id.sequence
            previous = self._last_sample.get(key, -1)
            if (
                type(seq) is not int
                or seq < 0
                or seq < previous
                or (key == (False, self._trigger) and seq == previous)
            ):
                return self._reject(item, NoCommand.DUPLICATE)
            received = sample.received_monotonic_ns
            if type(received) is not int or not self._started_ns <= received <= now:
                return self._reject(item, NoCommand.EXPIRED)
            deadline = min(deadline, received + self._age_ns)
            sequences[key] = seq
        item = _Submission(request, deadline)
        if (False, self._trigger) not in sequences:
            return self._reject(item, NoCommand.INVALID_INPUTS)
        if now >= deadline:
            return self._reject(item, NoCommand.EXPIRED)
        self._last_sample.update(sequences)
        self._last_request = identity.sequence
        self._event("submitted", item)
        if not self._active or identity.generation != self._generation:
            return self._reject(item, NoCommand.INACTIVE)
        item.timer = self._loop.call_later(
            (deadline - self._clock()) / 1e9, self._expire, item
        )
        if self._task is None:
            self._launch(item)
        else:
            previous, self._pending = self._pending, item
            if previous:
                self._reject(previous, NoCommand.SUPERSEDED)
        return None

    def _expire(self, item):
        if self._current is item:
            self._current = None
            self._task.cancel()
        elif self._pending is item:
            self._pending = None
        else:
            return
        self._reject(item, NoCommand.EXPIRED)

    def _launch(self, item):
        self._current = item
        self._task = self._loop.create_task(self._run_one(item))
        self._task.add_done_callback(self._finished)

    async def _run_one(self, item):
        if self._clock() >= item.expires_ns:
            self._current = None
            self._reject(item, NoCommand.EXPIRED)
            return
        try:
            evaluation = await self._evaluate(item.request)
        except asyncio.TimeoutError:
            if self._current is item:
                self._current = None
                self._reject(item, NoCommand.EXPIRED)
            return
        except Exception:
            if self._current is item:
                self._current = None
                self._reject(item, NoCommand.PROVIDER_FAILED)
            return
        if self._current is not item:
            return
        self._current = None
        item.timer.cancel()
        result = evaluation.result
        if result.request_id != item.request.id or result.samples != sample_ids(
            item.request
        ):
            self._reject(item, NoCommand.MISMATCHED_ANSWER, evaluation.details)
            return
        if self._clock() >= item.expires_ns:
            self._reject(item, NoCommand.EXPIRED, evaluation.details)
            return
        result = replace(result, expires_monotonic_ns=item.expires_ns)
        evaluation = replace(evaluation, result=result)
        self._event("result", item, evaluation)
        if (
            isinstance(result.outcome, NoCommand)
            or not self._active
            or self._generation != result.request_id.generation
        ):
            return
        if self._clock() >= item.expires_ns:
            self._reject(item, NoCommand.EXPIRED, evaluation.details)
            return
        self._sink(result)
        self._event("dispatched", item, evaluation)

    def _finished(self, task):
        self._current = None
        self._task = None
        if not task.cancelled() and task.exception() is not None:
            self.stop()
            self._loop.call_exception_handler(
                {"message": "Policy callback failed", "exception": task.exception()}
            )
        if self._active and self._pending:
            item, self._pending = self._pending, None
            self._launch(item)

    def stop(self):
        self._check_loop()
        self._active = False
        pending, current = self._pending, self._current
        self._pending = self._current = None
        if self._task and current:
            self._task.cancel()
        for item in (current, pending):
            if item and (item is pending or not self._task.done()):
                self._reject(item, NoCommand.INACTIVE)

    async def close(self):
        self._closed = True
        self.stop()
        if self._task:
            done, _ = await asyncio.wait({self._task}, timeout=self._deadline_ns / 1e9)
            if not done:
                raise TimeoutError(
                    "Policy provider did not shut down within its deadline"
                )
