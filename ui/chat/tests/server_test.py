import asyncio
import unittest

from aiohttp import ClientSession, web
from aiohttp.test_utils import TestClient, TestServer
from openai import AsyncOpenAI

from ui.chat.server import Chat, app_for

ROUTE_FOR = {
    "What is a servo?": "answer",
    "How does a servomotor work?": "answer",
    "Which nodes are online?": "nodes",
    "List the ROS participants.": "nodes",
    "Look at it.": "clarify",
    "Pick up the mug.": "unsupported",
    "What can you see?": "unsupported",
}
ANSWER = "A servo uses feedback to reach a target position."


class ChatTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.calls = []
        self.confidence = 0.95
        self.malformed = self.refused = self.disconnected = False
        self.gate = asyncio.Event()
        self.slow = False
        services = web.Application()
        services.router.add_post("/v1/decisions", self.decision)
        services.router.add_post("/v1/responses", self.answer)
        services.router.add_get("/@/*/ros2/node/**", self.nodes)
        backend = TestServer(services)
        await backend.start_server()
        self.addAsyncCleanup(backend.close)
        self.api = AsyncOpenAI(
            api_key="offline", base_url=str(backend.make_url("/v1")), max_retries=0
        )
        self.addAsyncCleanup(self.api.close)
        http = ClientSession()
        self.addAsyncCleanup(http.close)
        self.chat = Chat(self.api, http, str(backend.make_url("/")))
        self.client = TestClient(TestServer(app_for(self.chat)))
        await self.client.start_server()
        self.addAsyncCleanup(self.client.close)
        self.token = (
            await (await self.client.post("/api/chat/session", json={})).json()
        )["session"]

    async def decision(self, request):
        self.calls.append("decisions")
        body = await request.json()
        self.assertEqual(body["model"], "gpt-6-luna")
        data = body["input"][0]["content"][0]["data"]
        self.history = data["history"]
        (question,) = body["questions"]
        self.choices = [c["value"] for c in question["choices"]]
        label = ROUTE_FOR[data["message"]]
        answer = {
            "name": question["name"] if not self.malformed else "old_turn",
            "type": "choice",
            "choice": label,
            "confidence": self.confidence,
            "probabilities": [
                {"value": v, "probability": float(v == label)} for v in self.choices
            ],
        }
        if self.refused:
            answer = {"name": question["name"], "type": "refusal"}
        if self.slow:
            self.gate.set()
            await asyncio.sleep(0.15)
        return web.json_response(
            {"model": body["model"], "answers": [answer], "usage": {"total_tokens": 10}}
        )

    async def answer(self, request):
        self.calls.append("responses")
        body = await request.json()
        self.assertNotIn("tools", body)
        self.assertFalse(body["store"])
        self.assertLessEqual(body["max_output_tokens"], 384)
        return web.json_response(
            {
                "id": "resp_fixture",
                "status": "completed",
                "output": [
                    {
                        "type": "message",
                        "role": "assistant",
                        "content": [{"type": "output_text", "text": ANSWER}],
                    }
                ],
            }
        )

    async def nodes(self, request):
        self.calls.append("graph")
        if self.disconnected:
            raise web.HTTPServiceUnavailable()
        return web.json_response(
            [{"key": "@/bridge/ros2/node/gid/workshop/encoder"}] * 2
        )

    async def post(self, seq, text):
        return await self.client.post(
            "/api/chat/turn",
            headers={"Authorization": "Bearer " + self.token},
            json={"sequence": seq, "text": text},
        )

    async def test_routes_from_authenticated_turns(self):
        for i, (text, route) in enumerate(ROUTE_FOR.items(), 1):
            with self.subTest(text=text):
                self.calls.clear()
                response = await self.post(i, text)
                self.assertEqual(response.status, 200)
                result = await response.json()
                self.assertEqual((result["route"], result["sequence"]), (route, i))
                self.assertEqual(len(self.history), 2 * min(4, i - 1))
                self.assertEqual(
                    self.calls,
                    ["decisions"]
                    + {"answer": ["responses"], "nodes": ["graph"]}.get(route, []),
                )
                if route == "answer":
                    self.assertEqual(result["reply"], ANSWER)
                if route == "nodes":
                    self.assertEqual(result["reply"].count("/workshop/encoder"), 1)
                    self.assertIn("server UTC", result["reply"])
        self.assertEqual(
            set(self.choices), {"nodes", "answer", "clarify", "unsupported"}
        )

    async def test_failures(self):
        self.disconnected = True
        self.assertEqual(
            (await (await self.post(1, "Which nodes are online?")).json())["outcome"],
            "unavailable",
        )
        self.calls.clear()
        self.refused = True
        self.assertEqual(
            (await (await self.post(2, "Which nodes are online?")).json())["outcome"],
            "refused",
        )
        self.refused, self.malformed = False, True
        self.assertEqual(
            (await (await self.post(3, "Which nodes are online?")).json())["outcome"],
            "failed",
        )
        self.malformed, self.confidence = False, 0.79
        data = await (await self.post(4, "Which nodes are online?")).json()
        self.assertEqual(
            (data["route"], data["outcome"]), ("clarify", "low_confidence")
        )
        self.assertEqual(self.calls, ["decisions"] * 3)

    async def test_timeout_and_deduplication(self):
        self.slow = True
        first = asyncio.create_task(self.post(1, "Which nodes are online?"))
        await asyncio.wait_for(self.gate.wait(), 2)
        self.assertEqual((await self.post(1, "Which nodes are online?")).status, 409)
        result = await (await first).json()
        self.assertEqual(
            await (await self.post(1, "Which nodes are online?")).json(), result
        )
        self.assertEqual((await self.post(1, "What is a servo?")).status, 409)
        self.assertEqual(self.calls, ["decisions", "graph"])
        self.chat.deadline = 0.04
        self.assertEqual(
            (await (await self.post(2, "What is a servo?")).json())["outcome"],
            "timeout",
        )
        self.assertNotIn("responses", self.calls)

    async def test_session_boundaries(self):
        self.chat.graph_url = ""
        await self.post(1, "What is a servo?")
        self.assertNotIn("nodes", self.choices)
        self.assertEqual((await self.post(3, "Look at it.")).status, 409)
        self.assertEqual((await self.post(2, " ")).status, 400)
        self.token = "unknown"
        self.assertEqual((await self.post(2, "Look at it.")).status, 410)
        self.assertEqual((await self.client.post("/api/chat/session")).status, 415)
        self.chat.api = None
        self.assertEqual(
            (await self.client.post("/api/chat/session", json={})).status, 503
        )


if __name__ == "__main__":
    unittest.main()
