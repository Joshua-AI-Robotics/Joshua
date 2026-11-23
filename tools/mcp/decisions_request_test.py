import asyncio
import json
import unittest
from unittest.mock import AsyncMock

import httpx2
from mcp.shared.memory import create_connected_server_and_client_session
from openai import AsyncOpenAI

from tools.mcp.decisions_request import DecisionsRequests
from tools.mcp.decisions_server import make_server
from tools.mcp.snapshot_observer import SnapshotObserver


class DecisionsRequestTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.route = "answer"
        self.confidence = 0.97
        self.calls = []
        self.delay = False
        self.entered = asyncio.Event()
        self.api = AsyncOpenAI(
            api_key="test",
            max_retries=0,
            http_client=httpx2.AsyncClient(transport=httpx2.MockTransport(self.handle)),
        )
        self.addAsyncCleanup(self.api.close)
        self.observer = SnapshotObserver()
        self.addAsyncCleanup(self.observer.aclose)
        self.requests = DecisionsRequests(self.api, self.observer)
        self.server = make_server(self.requests)

    async def handle(self, request):
        body = json.loads(request.content)
        op = request.url.path.split("/")[-1]
        self.calls.append(op)
        if op == "responses":
            self.answer_input = body["input"]
            self.assertFalse(body["store"])
            self.assertNotIn("tools", body)
            return httpx2.Response(
                200,
                json={
                    "id": "reply",
                    "status": "completed",
                    "output": [
                        {
                            "type": "message",
                            "role": "assistant",
                            "content": [
                                {
                                    "type": "output_text",
                                    "text": "A servo uses feedback.",
                                }
                            ],
                        }
                    ],
                },
            )
        if self.delay:
            self.entered.set()
            await asyncio.Event().wait()
        (question,) = body["questions"]
        labels = [c["value"] for c in question["choices"]]
        self.assertEqual(body["model"], "gpt-6-luna")
        self.parts = body["input"][0]["content"]
        ans = {
            "type": "choice",
            "name": question["name"],
            "choice": self.route,
            "confidence": self.confidence,
            "probabilities": [
                {"value": k, "probability": float(k == self.route)} for k in labels
            ],
        }
        if self.route == "refusal":
            ans.update(type="refusal")
        return httpx2.Response(
            200,
            json={
                "model": body["model"],
                "answers": [ans],
                "usage": {"total_tokens": 9},
            },
        )

    async def test_mcp_routes_whole_request_and_retries(self):
        async with create_connected_server_and_client_session(self.server) as client:
            (tool,) = (await client.list_tools()).tools
            self.assertEqual(tool.name, "request_decision")
            self.assertTrue(tool.annotations.openWorldHint)
            for seq, (text, route, outcome) in enumerate(
                [
                    ("How does a servo work?", "answer", "completed"),
                    ("What is running?", "status", "unavailable"),
                    ("Look at it.", "clarify", "completed"),
                    ("Lift the mug.", "unsupported", "completed"),
                    ("Try this again.", "refusal", "refused"),
                ],
                1,
            ):
                self.route = route
                self.calls.clear()
                args = {"text": text, "sequence": seq}
                result = await client.call_tool(tool.name, args)
                self.assertFalse(result.isError)
                data = result.structuredContent
                self.assertEqual(data["outcome"], outcome)
                self.assertEqual(data["route"], None if route == "refusal" else route)
                self.assertEqual(self.parts[-1]["text"], text)
                self.assertEqual(
                    self.calls,
                    ["decisions"] + (["responses"] if route == "answer" else []),
                )
                self.calls.clear()
                duplicate = await client.call_tool(tool.name, args)
                self.assertEqual(duplicate.structuredContent, data)
                self.assertEqual(self.calls, [])
            self.assertTrue(
                (
                    await client.call_tool(tool.name, {"text": "wrong", "sequence": 1})
                ).isError
            )
            self.assertTrue(
                (
                    await client.call_tool(
                        tool.name, {"text": "a", "sequence": 6, "config": "/tmp"}
                    )
                ).isError
            )

    async def test_status_quality_and_invalid_choices(self):
        observed = await self.observer.get_runtime_status()
        self.route = "status"
        observed.update(
            status_code="OK",
            availability="fresh",
            instance_id="run1",
            snapshot_sequence=5,
            snapshot_age_seconds=0.5,
            data={
                "phase": "running",
                "processes": [{"node_name": "camera"}],
                "tracked_process_count": 1,
            },
        )
        self.observer.get_runtime_status = AsyncMock(return_value=observed)
        data = await self.requests.request("What is running?", 1)
        self.assertIn("camera", data["reply"])
        self.assertEqual(data["observation"]["instance_id"], "run1")
        observed["availability"] = "stale"
        self.assertEqual((await self.requests.request("Now?", 2))["outcome"], "stale")
        self.confidence = 0.79
        data = await self.requests.request("Look at it.", 3)
        self.assertEqual(
            (data["route"], data["outcome"]), ("clarify", "low_confidence")
        )
        self.route = "unknown"
        self.assertEqual(
            (await self.requests.request("Again.", 4))["outcome"], "failed"
        )
        self.assertNotIn("responses", self.calls)

    async def test_status_followup_uses_the_displayed_processes(self):
        observed = await self.observer.get_runtime_status()
        observed.update(
            availability="fresh",
            instance_id="private-instance",
            snapshot_age_seconds=0.5,
            data={
                "phase": "running",
                "processes": [
                    {"node_name": "camera"},
                    {"node_name": "tracker", "pid": 987654},
                ],
                "tracked_process_count": 2,
            },
        )
        self.observer.get_runtime_status = AsyncMock(return_value=observed)
        self.route = "status"
        first = await self.requests.request("What is running?", 1)
        self.route = "answer"
        await self.requests.request("Which process did you list second?", 2)
        history = self.parts[0]["data"]["previous_turns"]
        self.assertIn("camera, tracker", first["reply"])
        self.assertEqual(history[0]["reply"], first["reply"])
        self.assertIn(
            {"role": "assistant", "content": first["reply"]}, self.answer_input
        )
        sent = json.dumps([history, self.answer_input])
        self.assertNotIn("private-instance", sent)
        self.assertNotIn("987654", sent)

    async def test_timeout_settles_before_routing_provider_drains(self):
        entered = asyncio.Event()
        cancelled = asyncio.Event()
        release = asyncio.Event()

        async def slow_route(request):
            entered.set()
            try:
                await release.wait()
            except asyncio.CancelledError:
                cancelled.set()
                await release.wait()
            return await self.handle(request)

        async with AsyncOpenAI(
            api_key="test",
            max_retries=0,
            http_client=httpx2.AsyncClient(transport=httpx2.MockTransport(slow_route)),
        ) as api:
            requests = DecisionsRequests(api, self.observer, deadline=0.1)
            task = asyncio.create_task(requests.request("What is a servo?", 1))
            try:
                await asyncio.wait_for(entered.wait(), 1)
                worker = requests.active
                await asyncio.wait_for(cancelled.wait(), 2)
                result = await asyncio.wait_for(asyncio.shield(task), 1)
                self.assertEqual(result["outcome"], "timeout")
            finally:
                release.set()
                await task
            await asyncio.gather(worker, return_exceptions=True)
            self.assertEqual(self.calls, ["decisions"])
            self.assertIsNone(result["route"])
            self.assertEqual(await requests.request("What is a servo?", 1), result)

    async def test_timeout_cancellation_and_overlap(self):
        self.delay = True
        self.requests.deadline = 0.05
        self.assertEqual(
            (await self.requests.request("First", 1))["outcome"], "timeout"
        )
        self.entered.clear()
        self.requests.deadline = 2
        active = asyncio.create_task(self.requests.request("Second", 2))
        await asyncio.wait_for(self.entered.wait(), 1)
        with self.assertRaises(ValueError):
            await self.requests.request("Second", 2)
        active.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await active
        self.assertEqual(
            (await self.requests.request("Second", 2))["outcome"], "cancelled"
        )
        self.assertEqual(self.calls, ["decisions", "decisions"])


if __name__ == "__main__":
    unittest.main()
