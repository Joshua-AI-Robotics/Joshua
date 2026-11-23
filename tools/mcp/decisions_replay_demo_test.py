import io
import json
import unittest
from contextlib import redirect_stderr, redirect_stdout
from unittest.mock import AsyncMock, call

from mcp.types import CallToolResult, TextContent

from tools.mcp.decisions_replay_demo import CASES, run_turns


def turn(sequence, route, outcome, **details):
    return CallToolResult(
        content=[],
        structuredContent={
            "sequence": sequence,
            "route": route,
            "outcome": outcome,
            "reply": "Replay reply.",
            **details,
        },
    )


class ReplayDemoTest(unittest.IsolatedAsyncioTestCase):
    async def run_cases(self, results):
        client = AsyncMock()
        client.call_tool.side_effect = results
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(io.StringIO()):
            ok = await run_turns(client)
        self.assertEqual(
            client.call_tool.await_args_list,
            [
                call("request_decision", {"sequence": sequence, "text": case[0]})
                for sequence, case in enumerate(CASES, 1)
            ],
        )
        events = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(
            [e["event"] for e in events], ["request", "result"] * 3 + ["summary"]
        )
        self.assertEqual(events[-1]["passed"], sum(e.get("ok", False) for e in events))
        self.assertEqual(events[-1]["total"], len(CASES))
        return ok, [e for e in events if e["event"] == "result"]

    async def test_visible_target_visual_answer_and_absent_target(self):
        for outcome in ("low_confidence", "no_command"):
            with self.subTest(outcome=outcome):
                ok, records = await self.run_cases(
                    [
                        turn(
                            1,
                            "head",
                            "completed",
                            replay_command={"joint": "pan", "radians": 0.1},
                        ),
                        turn(2, "scene", "completed"),
                        turn(3, "head", outcome, motor={"choice": "missing"}),
                    ]
                )
                self.assertTrue(ok)
                self.assertEqual(records[2]["report"]["outcome"], outcome)

    async def test_wrong_joint_false_clarification_and_wrong_no_command_reason(self):
        ok, records = await self.run_cases(
            [
                turn(
                    1,
                    "head",
                    "completed",
                    replay_command={"joint": "tilt", "radians": 0.1},
                ),
                turn(2, "clarify", "low_confidence"),
                turn(3, "head", "no_command", motor={"choice": "centered"}),
            ]
        )
        self.assertFalse(ok)
        self.assertEqual([e["ok"] for e in records], [False, False, False])
        self.assertFalse(records[0]["matched"]["command"])
        self.assertTrue(records[1]["matched"]["command"])
        self.assertFalse(records[1]["matched"]["route"])
        self.assertFalse(records[2]["matched"]["missing_target"])

    async def test_tool_errors_and_unexpected_motion_stay_in_report(self):
        error = CallToolResult(
            isError=True, content=[TextContent(type="text", text="Tool error.")]
        )
        ok, records = await self.run_cases(
            [
                error,
                turn(
                    2,
                    "scene",
                    "completed",
                    replay_command={"joint": "pan", "radians": -0.1},
                ),
                turn(3, "head", "failed"),
            ]
        )
        self.assertFalse(ok)
        self.assertEqual([e["ok"] for e in records], [False, False, False])
        self.assertTrue(records[0]["tool_error"])
        self.assertEqual(records[0]["reply"], "Tool error.")
        self.assertFalse(records[1]["matched"]["command"])
        self.assertFalse(records[2]["matched"]["outcome"])

    async def test_malformed_receipts_are_recorded_and_scored(self):
        ok, records = await self.run_cases(
            [
                turn(1, "head", "no_command", motor="missing"),
                turn(2, "scene", "completed", reply=["invalid"]),
                turn(3, "head", "no_command", motor={"choice": "missing"}),
            ]
        )
        self.assertFalse(ok)
        self.assertEqual([e["ok"] for e in records], [False, False, True])
        self.assertEqual(records[0]["report"]["motor"], "missing")
        self.assertFalse(records[0]["matched"]["missing_target"])
        self.assertEqual(records[1]["report"]["reply"], ["invalid"])
        self.assertFalse(records[1]["matched"]["reply"])


if __name__ == "__main__":
    unittest.main()
