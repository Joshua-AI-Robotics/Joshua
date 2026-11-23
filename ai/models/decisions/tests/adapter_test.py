import asyncio
import json
import os
import subprocess
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

import httpx2
from google.protobuf import text_format
from openai import AsyncOpenAI
from robot.action.proto.action_packet_pb2 import JointCommand

from ai.inference.motor_policy import (
    EvidenceSample,
    MeasuredPosition,
    NoCommand,
    PolicyRequest,
    RequestId,
    Rgb8,
    SampleId,
)
from ai.models.decisions import adapter as decisions
from ai.models.registry import create_adapter
from config.proto.config_pb2 import Config


class DecisionsTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        root = Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"]
        path = root / "config/config_preset/example/decisions_indexer_replay.pbtxt"
        subprocess.run(
            [root / "config/joshua_validate_config", "--config", path],
            check=True,
            capture_output=True,
        )
        cfg = text_format.Parse(path.read_text(), Config())
        model = cfg.ai.models.single_models[0]
        settings = model.decisions_config
        self.adapter = create_adapter(model, cfg)
        self.settings = settings
        self.calls = []
        self.answer = {
            "type": "choice",
            "name": "index",
            "choice": "advance",
            "confidence": 0.8,
            "probabilities": [
                {"value": v, "probability": p}
                for v, p in (("advance", 0.9), ("home", 0.1), ("hold", 0.0))
            ],
        }
        self.response = {
            "answers": [self.answer],
            "model": settings.model,
            "usage": {"total_tokens": 20},
        }
        self.status = 200

        async def handle(request):
            self.calls.append(json.loads(request.content))
            self.assertEqual(request.url.path, "/v1/decisions")
            return httpx2.Response(
                self.status,
                content=json.dumps(self.response).encode(),
                headers={
                    "Content-Type": "application/json",
                    "x-request-id": "req-test",
                },
            )

        self.client = AsyncOpenAI(
            api_key="offline-test",
            http_client=httpx2.AsyncClient(transport=httpx2.MockTransport(handle)),
        )
        self.addAsyncCleanup(self.client.close)
        self.events = []
        self.sink = []
        self.result = asyncio.Event()
        self.seq = 0
        self.inspection = '{"ready":true}'

    def record(self, event):
        self.events.append(event)
        if event.phase == "result":
            self.result.set()

    def request(self, generation):
        now = time.monotonic_ns()
        self.seq += 1

        def sample(name, value):
            return EvidenceSample(name, SampleId(name, 0, self.seq), value, now)

        return PolicyRequest(
            1,
            RequestId("run", "cfg", generation, self.seq),
            "index",
            (
                sample("workpiece", Rgb8(1, 1, b"rgb")),
                sample("instruction", "Inspect"),
                sample("inspection", self.inspection),
            ),
            MeasuredPosition("indexer", SampleId("encoder", 0, self.seq), 0.5, 1, now),
        )

    async def replay(self):
        async with self.adapter.session(
            run="run",
            config_id="cfg",
            sink=self.sink.append,
            record=self.record,
            client=self.client,
        ) as session:
            generation = session.start(
                trigger="workpiece",
                sources={k: (k, 0) for k in ("workpiece", "instruction", "inspection")},
                feedback=("encoder", 0),
            )
            self.result.clear()
            req = self.request(generation)
            self.assertIsNone(session.submit(req))
            await asyncio.wait_for(self.result.wait(), 3)
        return next(e.evaluation for e in reversed(self.events) if e.phase == "result")

    async def test_registered_adapter_and_sdk(self):
        self.assertTrue(self.adapter.spec().HasField("motor_policy"))
        with self.assertRaises(ValueError):
            self.adapter.on_observation(None)
        result = await self.replay()
        self.assertEqual(self.sink[0].expires_monotonic_ns, self.events[0].expires_ns)
        cmd = self.sink[0].outcome
        self.assertEqual(
            (cmd.joint_name, cmd.position, cmd.position_encoding),
            ("indexer", 0.625, JointCommand.POSITION_SI),
        )
        self.assertEqual(result.details["tokens"]["total_tokens"], 20)
        self.assertEqual(result.details["api_request_id"], "req-test")
        self.assertEqual(
            [e.phase for e in self.events], ["submitted", "result", "dispatched"]
        )
        (body,) = self.calls
        self.assertEqual(
            body["questions"][0]["choices"],
            [{"value": v} for v in ("advance", "home", "hold")],
        )
        parts = body["input"][0]["content"]
        self.assertTrue(parts[1]["image_url"].startswith("data:image/png;base64,iVBOR"))
        self.assertEqual(parts[3], {"type": "input_text", "text": "Inspect"})
        self.assertEqual(
            parts[6]["data"]["measured_position"],
            {"joint": "indexer", "value": 0.5, "unit": "RADIANS"},
        )
        self.assertEqual(parts[5], {"type": "input_json", "data": {"ready": True}})

    async def test_untrusted_answers(self):
        for changes, reason in [
            ({"type": "refusal"}, NoCommand.REFUSED),
            ({"confidence": 0.79}, NoCommand.LOW_CONFIDENCE),
            ({"choice": "other"}, NoCommand.MALFORMED_RESPONSE),
            ({"name": "other"}, NoCommand.MISMATCHED_ANSWER),
            ({"confidence": float("inf")}, NoCommand.MALFORMED_RESPONSE),
            ({"confidence": True}, NoCommand.MALFORMED_RESPONSE),
            (
                {"probabilities": [{"value": "home", "probability": 0.3}] * 3},
                NoCommand.MALFORMED_RESPONSE,
            ),
            ({"type": "predicate"}, NoCommand.MALFORMED_RESPONSE),
            ({"choice": "hold"}, NoCommand.NO_COMMAND),
        ]:
            with self.subTest(changes=changes):
                self.response["answers"] = [{**self.answer, **changes}]
                self.assertEqual((await self.replay()).result.outcome, reason)
        self.assertFalse(self.sink)

    async def test_json_array_requires_objects(self):
        self.inspection = '[{"ready":true}]'
        self.assertEqual((await self.replay()).result.outcome.joint_name, "indexer")
        self.inspection = "[true]"
        self.assertEqual(
            (await self.replay()).result.outcome, NoCommand.INVALID_EVIDENCE
        )
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(len(self.sink), 1)

    async def test_cancellation_drains_encoding_before_releasing_worker(self):
        loop = asyncio.get_running_loop()
        started = asyncio.Event()
        release = threading.Event()

        def encode(settings, request):
            loop.call_soon_threadsafe(started.set)
            release.wait(3)
            return None, {}

        with patch.object(decisions, "_request_body", encode):
            task = asyncio.create_task(
                decisions._evaluate(self.settings, self.client, self.request(1))
            )
            try:
                await asyncio.wait_for(started.wait(), 1)
                task.cancel()
                await asyncio.sleep(0)
                self.assertFalse(task.done())
            finally:
                release.set()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(task, 1)
        self.assertFalse(self.calls)

    async def test_outage_and_retry_budget(self):
        for self.status, reason in (
            (429, NoCommand.RATE_LIMITED),
            (503, NoCommand.PROVIDER_FAILED),
        ):
            self.calls.clear()
            self.assertEqual((await self.replay()).result.outcome, reason)
            self.assertEqual(len(self.calls), 1)
        self.settings.max_attempts = 2
        self.calls.clear()
        self.assertEqual(
            (await self.replay()).result.outcome, NoCommand.PROVIDER_FAILED
        )
        self.assertEqual(len(self.calls), 2)
        self.assertFalse(self.sink)


if __name__ == "__main__":
    unittest.main()
