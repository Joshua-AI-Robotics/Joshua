"""Adversarial framing and lifecycle checks using real disposable child processes."""

import asyncio
import os
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.mcp.image_source import ImageSource
from tools.mcp.observation_source import UnavailableReason


class ImageLifecycleTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.sources = []

    async def asyncTearDown(self):
        for source in self.sources:
            await source.aclose()
        self.temp.cleanup()

    def source(self, code, camera_id="sensor:webcam"):
        helper = Path(self.temp.name, "helper" + str(len(self.sources)))
        helper.write_text(
            "#!/usr/bin/python3\nimport sys,time,struct,json,os\n"
            "q=int(sys.stdin.readline())\n" + code
        )
        helper.chmod(0o700)
        source = ImageSource(self.temp.name, str(helper), camera_id)
        self.sources.append(source)
        return source

    async def pending(self, source):
        task = asyncio.create_task(source.read_latest())
        for _ in range(100):
            if source._process is not None:
                return task, source._process
            await asyncio.sleep(0.01)
        self.fail("helper did not spawn")

    async def test_overlap_close_resolves_consumer_without_cancelling_it(self):
        source = self.source("time.sleep(60)\n")
        task, process = await self.pending(source)
        self.assertEqual(
            (await source.read_latest()).unavailable_reason, UnavailableReason.BUSY
        )
        await asyncio.gather(source.aclose(), source.aclose())
        self.assertEqual((await task).unavailable_reason, UnavailableReason.CLOSED)
        self.assertFalse(task.cancelled())
        self.assertIsNotNone(process.returncode)
        self.assertEqual(
            (await source.read_latest()).unavailable_reason, UnavailableReason.CLOSED
        )

    async def test_caller_cancellation_reaps_before_propagating(self):
        source = self.source("time.sleep(60)\n")
        task, process = await self.pending(source)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertIsNotNone(process.returncode)

    async def test_default_five_second_timeout_reaps_process(self):
        source = self.source("time.sleep(60)\n")
        started = time.monotonic()
        result = await source.read_latest()
        self.assertEqual(result.unavailable_reason, UnavailableReason.TIMEOUT)
        self.assertLess(time.monotonic() - started, 7.2)
        self.assertIsNone(source._process)

    async def test_bounded_frames_correlation_truncation_and_stderr(self):
        reply = (
            'm=json.dumps(dict(query_id=q,camera_id="sensor:webcam",'
            'availability="unavailable",reason="no_data",diagnostic="none",'
            "reset=False)).encode()\n"
            'os.write(1,struct.pack("!II",len(m),0)+m)\n'
        )
        for code, reason in [
            (
                'os.write(1,struct.pack("!II",16385,0)); time.sleep(60)\n',
                UnavailableReason.INVALID_DATA,
            ),
            (
                'os.write(1,struct.pack("!II",1,524289)); time.sleep(60)\n',
                UnavailableReason.INVALID_DATA,
            ),
            ("q+=1\n" + reply, UnavailableReason.INVALID_DATA),
            (
                'os.write(1,struct.pack("!II",10,0)+b"xx")\n',
                UnavailableReason.BACKEND_ERROR,
            ),
            (
                'os.write(2,b"x"*70000); time.sleep(.05)\n' + reply,
                UnavailableReason.INVALID_DATA,
            ),
        ]:
            source = self.source(code)
            result = await source.read_latest()
            self.assertEqual(result.unavailable_reason, reason, result)
            self.assertIsNone(source._process)

    async def test_valid_unicode_identity_is_enabled_by_exact_match(self):
        source = self.source("time.sleep(60)\n", "sensor:front camera Ω")
        self.assertEqual(source.describe().source_id, "sensor:front camera Ω")

    async def test_late_spawn_is_owned_and_reaped_on_cancellation(self):
        source = self.source("time.sleep(60)\n")
        original = asyncio.create_subprocess_exec
        processes = []

        async def delayed(*args, **kwargs):
            process = await original(*args, **kwargs)
            processes.append(process)
            await asyncio.sleep(0.25)
            return process

        with patch("tools.mcp.image_source.asyncio.create_subprocess_exec", delayed):
            task = asyncio.create_task(source.read_latest())
            await asyncio.sleep(0.05)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
        self.assertEqual(len(processes), 1)
        self.assertIsNotNone(processes[0].returncode)

    async def test_failed_cleanup_poison_prevents_new_helper(self):
        source = self.source("time.sleep(60)\n")
        task, process = await self.pending(source)
        original = process.wait

        async def hangs():
            await asyncio.sleep(60)

        process.wait = hangs
        started = time.monotonic()
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertLess(time.monotonic() - started, 2.2)
        self.assertTrue(source._poisoned)
        self.assertEqual(
            (await source.read_latest()).unavailable_reason,
            UnavailableReason.BACKEND_ERROR,
        )
        process.wait = original
        await source.aclose()

    async def test_spawn_wait_and_reaping_share_one_cleanup_budget(self):
        source = self.source("time.sleep(60)\n")
        original = asyncio.create_subprocess_exec
        processes = []

        async def delayed(*args, **kwargs):
            process = await original(*args, **kwargs)
            processes.append(process)
            actual_wait = process.wait

            async def slow_wait():
                await asyncio.sleep(1.5)
                return await actual_wait()

            process.wait = slow_wait
            await asyncio.sleep(1.5)
            return process

        with patch("tools.mcp.image_source.asyncio.create_subprocess_exec", delayed):
            task = asyncio.create_task(source.read_latest())
            await asyncio.sleep(0.05)
            started = time.monotonic()
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
            self.assertLess(time.monotonic() - started, 2.2)
            self.assertTrue(source._poisoned)
        await asyncio.sleep(0.1)
        self.assertIsNotNone(processes[0].returncode)

    async def test_unconfirmed_late_spawn_eventually_reaps_and_stays_poisoned(self):
        source = self.source("time.sleep(60)\n")
        original = asyncio.create_subprocess_exec
        processes = []

        async def delayed(*args, **kwargs):
            process = await original(*args, **kwargs)
            processes.append(process)
            await asyncio.sleep(2.3)
            return process

        with patch("tools.mcp.image_source.asyncio.create_subprocess_exec", delayed):
            task = asyncio.create_task(source.read_latest())
            await asyncio.sleep(0.05)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
        await asyncio.sleep(0.4)
        self.assertTrue(source._poisoned)
        self.assertIsNotNone(processes[0].returncode)

    async def test_repeated_caller_cancellation_waits_for_owned_cleanup(self):
        source = self.source("time.sleep(60)\n")
        task, process = await self.pending(source)
        actual_wait = process.wait

        async def delayed_wait():
            await asyncio.sleep(0.2)
            return await actual_wait()

        process.wait = delayed_wait
        started = time.monotonic()
        task.cancel()
        await asyncio.sleep(0.05)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertGreater(time.monotonic() - started, 0.18)
        self.assertIsNotNone(process.returncode)

    async def test_close_during_late_spawn_preserves_duplicated_directory_fd(self):
        source = self.source("time.sleep(60)\n")
        original = asyncio.create_subprocess_exec
        processes = []
        observed = []

        async def delayed(*args, **kwargs):
            await asyncio.sleep(2.3)
            observed.append(os.fstat(kwargs["pass_fds"][0]).st_ino)
            process = await original(*args, **kwargs)
            processes.append(process)
            return process

        expected = os.fstat(source._fd).st_ino
        with patch("tools.mcp.image_source.asyncio.create_subprocess_exec", delayed):
            read = asyncio.create_task(source.read_latest())
            await asyncio.sleep(0.05)
            await source.aclose()
            self.assertEqual((await read).unavailable_reason, UnavailableReason.CLOSED)
            await asyncio.sleep(0.4)
        self.assertEqual(observed, [expected])
        self.assertIsNotNone(processes[0].returncode)

    async def test_delayed_concurrent_close_does_not_cancel_consumer(self):
        source = self.source("time.sleep(60)\n")
        task, process = await self.pending(source)
        actual_wait = process.wait

        async def slow():
            await asyncio.sleep(0.15)
            return await actual_wait()

        process.wait = slow
        first = asyncio.create_task(source.aclose())
        await asyncio.sleep(0.03)
        await source.aclose()
        await first
        self.assertEqual((await task).unavailable_reason, UnavailableReason.CLOSED)
        self.assertFalse(task.cancelled())


if __name__ == "__main__":
    unittest.main()
