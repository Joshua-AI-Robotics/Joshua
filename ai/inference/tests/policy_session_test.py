import asyncio
import unittest
from dataclasses import replace

from robot.action.proto.action_packet_pb2 import JointCommand

from ai.inference.motor_policy import (
    EvidenceSample,
    NoCommand,
    PolicyRequest,
    PolicyResult,
    RequestId,
    SampleId,
    sample_ids,
)
from ai.inference.policy_session import Evaluation, PolicySession


class PolicySessionTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.now = 2_000_000
        self.entered = asyncio.Queue()
        self.admitted = asyncio.Queue()
        self.events = []
        self.sink = []
        self.session = PolicySession(
            run="r",
            config="c",
            deadline_ms=1000,
            max_receipt_age_ms=100,
            evaluate=self.evaluate,
            record=self.record,
            sink=self.sink.append,
            clock=lambda: self.now,
        )
        self.addAsyncCleanup(self.session.close)
        self.gen = self.start(0)

    def record(self, event):
        self.events.append(event)
        if event.phase == "result":
            self.admitted.put_nowait(event.evaluation.result.outcome)

    def start(self, source_generation):
        return self.session.start(
            trigger="view", sources={"view": ("cam", source_generation)}
        )

    async def evaluate(self, request):
        gate = asyncio.get_running_loop().create_future()
        self.entered.put_nowait((request.id.sequence, gate))
        try:
            result_id = await asyncio.shield(gate)
        except asyncio.CancelledError:
            if not getattr(self, "complete_after_stop", False):
                raise
            self.cancel_seen.set()
            result_id = await gate
        return Evaluation(
            PolicyResult(
                result_id,
                sample_ids(request),
                JointCommand(
                    joint_name="j",
                    position=0.25,
                    position_encoding=JointCommand.POSITION_SI,
                ),
            ),
            {},
        )

    def req(self, n, source=0):
        return PolicyRequest(
            1,
            RequestId("r", "c", self.gen, n),
            "q",
            (EvidenceSample("view", SampleId("cam", source, n), "left", self.now),),
        )

    async def take(self, queue):
        return await asyncio.wait_for(queue.get(), 1)

    async def test_one_active_one_latest_pending(self):
        first = self.req(1)
        self.session.submit(first)
        _, gate = await self.take(self.entered)
        self.session.submit(self.req(2))
        self.session.submit(self.req(3))
        self.assertEqual(await self.take(self.admitted), NoCommand.SUPERSEDED)
        self.assertTrue(self.entered.empty())
        gate.set_result(first.id)
        self.assertIsInstance(await self.take(self.admitted), JointCommand)
        sequence, gate = await self.take(self.entered)
        self.assertEqual(sequence, 3)
        gate.set_result(self.req(3).id)
        self.assertIsInstance(await self.take(self.admitted), JointCommand)
        self.assertEqual([r.request_id.sequence for r in self.sink], [1, 3])

    async def test_stop_reset_and_sources(self):
        first = self.req(1)
        self.session.submit(first)
        await self.take(self.entered)
        self.session.submit(self.req(2))
        self.session.stop()
        self.assertEqual(await self.take(self.admitted), NoCommand.INACTIVE)
        self.assertEqual(await self.take(self.admitted), NoCommand.INACTIVE)
        self.assertEqual(self.session.submit(self.req(3)), NoCommand.INACTIVE)
        self.gen = self.start(1)
        self.assertEqual(self.session.submit(first), NoCommand.WRONG_SESSION)
        self.assertEqual(self.session.submit(self.req(3)), NoCommand.WRONG_SOURCE)
        for change in ({"config": "old"}, {"run": "old"}):
            req = self.req(3, 1)
            self.assertEqual(
                self.session.submit(replace(req, id=replace(req.id, **change))),
                NoCommand.WRONG_SESSION,
            )
        req = self.req(3, 1)
        self.assertIsNone(self.session.submit(req))
        seq, gate = await self.take(self.entered)
        self.assertEqual(seq, 3)
        gate.set_result(first.id)
        await self.session.close()
        self.assertFalse(self.sink)
        with self.assertRaises(ValueError):
            self.start(0)

    async def test_late_reply_cannot_dispatch_after_source_switch(self):
        self.complete_after_stop = True
        self.cancel_seen = asyncio.Event()
        old = self.req(1)
        self.session.submit(old)
        _, old_gate = await self.take(self.entered)
        self.session.stop()
        self.assertEqual(await self.take(self.admitted), NoCommand.INACTIVE)
        await asyncio.wait_for(self.cancel_seen.wait(), 1)
        self.gen = self.start(1)
        fresh = self.req(1, 1)
        self.session.submit(fresh)
        old_gate.set_result(old.id)
        _, fresh_gate = await self.take(self.entered)
        self.assertFalse(self.sink)
        fresh_gate.set_result(fresh.id)
        self.assertIsInstance(await self.take(self.admitted), JointCommand)
        self.assertEqual([r.request_id for r in self.sink], [fresh.id])

    async def test_age_duplicate_and_late_completion(self):
        stale = self.req(1)
        self.now += 101_000_000
        self.assertEqual(self.session.submit(stale), NoCommand.EXPIRED)
        await self.take(self.admitted)
        first = self.req(1)
        self.session.submit(first)
        _, gate = await self.take(self.entered)
        duplicate = replace(first, id=replace(first.id, sequence=2))
        self.assertEqual(self.session.submit(duplicate), NoCommand.DUPLICATE)
        await self.take(self.admitted)
        self.now += 100_000_000
        gate.set_result(first.id)
        self.assertEqual(await self.take(self.admitted), NoCommand.EXPIRED)
        self.assertFalse(self.sink)

    async def test_mismatched_completion(self):
        first = self.req(1)
        self.session.submit(first)
        _, gate = await self.take(self.entered)
        gate.set_result(self.req(2).id)
        self.assertEqual(await self.take(self.admitted), NoCommand.MISMATCHED_ANSWER)
        self.assertFalse(self.sink)

    async def test_slow_provider_expires_without_blocking_ingestion(self):
        self.session.submit(self.req(1))
        await self.take(self.entered)
        self.assertEqual(await self.take(self.admitted), NoCommand.EXPIRED)
        self.assertFalse(self.sink)


if __name__ == "__main__":
    unittest.main()
