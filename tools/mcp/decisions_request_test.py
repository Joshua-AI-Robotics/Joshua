import asyncio
import base64
import io
import json
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import AsyncMock

import httpx2
from mcp.shared.memory import create_connected_server_and_client_session
from openai import AsyncOpenAI
from PIL import Image

from ai.inference.manifest import repo_root
from tools.mcp.decisions_request import DecisionsRequests
from tools.mcp.decisions_server import make_server
from tools.mcp.head_replay import HeadReplay
from tools.mcp.snapshot_observer import SnapshotObserver


class DecisionsRequestTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.route = "answer"
        self.confidence = 0.97
        self.motor_confidence = 0.97
        self.calls = []
        self.binding = None
        self.hold_motor = False
        self.release = asyncio.Event()
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
            self.assertFalse(body["store"])
            self.assertNotIn("tools", body)
            self.answer_input = body["input"]
            answer = "A servo uses feedback."
            if isinstance(body["input"], list) and isinstance(
                body["input"][-1]["content"], list
            ):
                answer = "The red mug is on the right."
            if "text" in body:
                self.calls[-1] = "binding"
                self.assertTrue(body["text"]["format"]["strict"])
                answer = json.dumps({"target": self.binding})
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
                                    "text": answer,
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
        choice = self.route
        self.labels = labels
        if question["name"] == "target_location":
            self.calls[-1] = "motor"
            encoded = self.parts[1]["image_url"].split(",")[1]
            image = Image.open(io.BytesIO(base64.b64decode(encoded)))
            color = {"red mug": (207, 51, 44), "blue mug": (37, 77, 165)}.get(
                self.parts[3]["text"]
            )
            choice = next(
                (
                    label
                    for label, x in (("target_left", 60), ("target_right", 248))
                    if image.getpixel((x, 90)) == color
                ),
                "missing",
            )
            if self.hold_motor:
                self.entered.set()
                try:
                    await self.release.wait()
                except asyncio.CancelledError:
                    await self.release.wait()
        ans = {
            "type": "choice",
            "name": question["name"],
            "choice": choice,
            "confidence": (
                self.motor_confidence
                if question["name"] == "target_location"
                else self.confidence
            ),
            "probabilities": [
                {"value": k, "probability": float(k == choice)} for k in labels
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

    async def test_mcp_replay_labeled_turns(self):
        replay = HeadReplay.load()
        self.requests.bind_replay(replay)
        async with create_connected_server_and_client_session(
            make_server(self.requests)
        ) as client:
            names = {t.name for t in (await client.list_tools()).tools}
            self.assertEqual(names, {"request_decision", "stop_decision"})
            for seq, (text, route, position) in enumerate(
                [
                    ("Look at the red mug.", "head", 0.1),
                    ("Tell me where the mug is, don't move.", "scene", None),
                    ("Point your head at the blue mug.", "head", -0.1),
                    ("Look at the green bottle.", "head", None),
                    ("Can you look left?", "clarify", None),
                    ("Lift the mug.", "unsupported", None),
                ],
                1,
            ):
                self.route = route
                self.calls.clear()
                before = len(replay.commands)
                args = {"sequence": seq, "text": text}
                result = (
                    await client.call_tool("request_decision", args)
                ).structuredContent
                self.assertEqual(result["route"], route)
                self.assertEqual(
                    len(replay.commands) - before, int(position is not None)
                )
                self.assertEqual(
                    result.get("replay_command", {}).get("radians"), position
                )
                if position is not None:
                    command = replay.commands[-1]
                    self.assertEqual(command.request_id.run, result["turn_id"])
                    self.assertEqual(command.request_id.config, replay.config_id)
                elif route == "head":
                    self.assertEqual(result["outcome"], "no_command")
                if route == "scene":
                    self.assertEqual(
                        result["reply"],
                        "Synthetic camera replay\n\nThe red mug is on the right.",
                    )
                    self.assertEqual(
                        self.answer_input[-1]["content"][1]["image_url"],
                        replay.image_url,
                    )
                self.assertEqual(
                    self.calls,
                    ["decisions"]
                    + {"scene": ["responses"], "head": ["motor"]}.get(route, []),
                )
                self.calls.clear()
                repeat = await client.call_tool("request_decision", args)
                self.assertEqual(repeat.structuredContent, result)
                self.assertEqual(self.calls, [])

    async def test_replay_guards_and_freeform_target(self):
        replay = HeadReplay.load()
        self.requests.bind_replay(replay)
        self.route = "head"
        for seq, text in enumerate(
            (
                "Look at it.",
                "Tell me where it is, don't move.",
                "Don't point the head toward the mug.",
            ),
            1,
        ):
            report = await self.requests.request(text, seq)
            self.assertEqual(report["outcome"], "failed")
            self.assertNotIn("head", self.labels)
        text = "Please aim your gaze toward the blue mug for me."
        for seq, target in enumerate((None, "red mug", "blue mug"), 4):
            self.binding = target
            result = await self.requests.request(text, seq)
            if target == "blue mug":
                self.assertEqual(result["replay_command"]["radians"], -0.1)
            else:
                self.assertEqual(result["outcome"], "unbound_target")
                self.assertFalse(replay.commands)
        empty = HeadReplay(
            replay.adapter,
            replay.config_id,
            replay.question,
            replay.joint,
            Image.new("RGB", (320, 180)),
            0.0,
        )
        self.requests.bind_replay(empty)
        report = await self.requests.request("Look at the blue mug.", 7)
        self.assertEqual(report["motor"]["choice"], "missing")
        self.assertFalse(empty.commands)
        self.motor_confidence = 0.77
        report = await self.requests.request("Look at the green bottle.", 8)
        self.assertEqual(report["outcome"], "low_confidence")
        self.assertIn("uncertain", report["reply"])
        self.assertNotIn("missing", report["reply"])
        self.assertFalse(empty.commands)

    async def test_keep_head_still_withholds_motion_before_routing(self):
        replay = HeadReplay.load()
        self.requests.bind_replay(replay)
        self.binding = "red mug"
        text = "Keep the head still and tell me where the red mug is."
        self.route = "head"
        rejected = await self.requests.request(text, 1)
        self.assertNotIn("head", self.labels)
        self.assertEqual(rejected["outcome"], "failed")
        self.assertEqual(self.calls, ["decisions"])
        self.assertFalse(replay.commands)

        self.calls.clear()
        self.route = "scene"
        visual = await self.requests.request(text, 2)
        self.assertIn("red mug", visual["reply"])
        self.assertEqual(self.calls, ["decisions", "responses"])
        self.assertFalse(replay.commands)

        self.route = "head"
        motion = await self.requests.request(
            "Please still point your head at the red mug.", 3
        )
        self.assertEqual(motion["replay_command"], {"joint": "pan", "radians": 0.1})
        self.assertEqual(len(replay.commands), 1)

    async def test_stop_and_rebind_reject_late_motor_results(self):
        old = HeadReplay.load()
        self.requests.bind_replay(old)
        self.route, self.hold_motor = "head", True
        async with create_connected_server_and_client_session(
            make_server(self.requests)
        ) as client:
            args = {"sequence": 1, "text": "Look at the red mug."}
            pending = asyncio.create_task(client.call_tool("request_decision", args))
            await asyncio.wait_for(self.entered.wait(), 2)
            duplicate = await client.call_tool("request_decision", args)
            self.assertTrue(duplicate.isError)
            stop = await client.call_tool("stop_decision", {"sequence": 1})
            self.assertEqual(stop.structuredContent["status"], "stopped")
            self.release.set()
            self.assertEqual((await pending).structuredContent["outcome"], "cancelled")
            await client.call_tool("stop_decision", {"sequence": 2})
            args["sequence"] = 2
            self.assertEqual(
                (await client.call_tool("request_decision", args)).structuredContent[
                    "outcome"
                ],
                "cancelled",
            )
        fixture = (
            repo_root() / "config/config_preset/example/decisions_head_replay.pbtxt"
        )
        with TemporaryDirectory() as directory:
            path = Path(directory) / "new.pbtxt"
            path.write_text(
                fixture.read_text().replace(
                    "position_delta: 0.1", "position_delta: 0.05"
                )
            )
            new = HeadReplay.load(path)
        self.assertNotEqual(old.config_id, new.config_id)
        self.entered.clear()
        self.release.clear()
        pending = asyncio.create_task(self.requests.request("Look at the red mug.", 3))
        await asyncio.wait_for(self.entered.wait(), 2)
        self.requests.bind_replay(new)
        self.release.set()
        self.assertEqual((await pending)["outcome"], "cancelled")
        self.assertFalse(old.commands)
        self.assertFalse(new.commands)
        report = await self.requests.request("Look at the red mug.", 4)
        self.assertEqual(report["replay_command"]["radians"], 0.05)
        self.assertEqual(self.requests.stop(4), "completed")


if __name__ == "__main__":
    unittest.main()
