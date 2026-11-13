"""Adversarial framing and lifecycle checks using real disposable child processes."""

import asyncio
import os
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.mcp.image_source import ImageSource
from tools.mcp.observation_source import Availability, UnavailableReason


class ImageLifecycleTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.sources = []

    async def asyncTearDown(self):
        for source in self.sources:
            await source.aclose()
        self.temp.cleanup()

    def source(self, code, camera_id="sensor:webcam", config_path="candidate.pbtxt"):
        helper = Path(self.temp.name, "helper" + str(len(self.sources)))
        helper.write_text(
            "#!/usr/bin/python3\nimport sys,time,struct,json,os\n"
            "q=int(sys.stdin.readline())\n" + code
        )
        helper.chmod(0o700)
        source = ImageSource(self.temp.name, str(helper), camera_id, config_path)
        self.sources.append(source)
        return source

    async def test_workspace_root_needs_no_private_runtime_directory(self):
        os.chmod(self.temp.name, 0o755)
        source = self.source("time.sleep(60)\n")
        self.assertEqual(os.fstat(source._fd).st_mode & 0o777, 0o755)

    async def test_config_selection_rejects_unsafe_relative_references(self):
        for value in (
            None,
            "",
            "/candidate.pbtxt",
            "../candidate.pbtxt",
            "nested//candidate.pbtxt",
            "./candidate.pbtxt",
            "bad\x00path",
        ):
            with self.subTest(config_path=value), self.assertRaises(ValueError):
                self.source("time.sleep(60)\n", config_path=value)

    async def test_helper_receives_pinned_root_and_fixed_config_selection(self):
        root_inode = os.stat(self.temp.name).st_ino
        source = self.source(
            "assert sys.argv[1]=='--root-fd'\n"
            f"assert os.fstat(int(sys.argv[2])).st_ino=={root_inode}\n"
            "assert sys.argv[3:]==['--config','nested/candidate.pbtxt',"
            "'--camera-id','sensor:webcam']\n"
            "m=json.dumps(dict(query_id=q,camera_id='sensor:webcam',"
            "availability='unavailable',"
            "reason='no_data',diagnostic='native ROS only',reset=False)).encode()\n"
            "os.write(1,struct.pack('!II',len(m),0)+m)\n",
            config_path="nested/candidate.pbtxt",
        )
        result = await source.read_latest()
        self.assertEqual(result.unavailable_reason, UnavailableReason.NO_DATA, result)
        self.assertEqual(result.diagnostic, "native ROS only")

    async def test_invalid_ros_domain_is_rejected_before_spawning(self):
        for domain in ("", "-1", "233", "1.0", " 1", "１２", "0000"):
            with self.subTest(domain=domain), patch.dict(
                os.environ, ROS_DOMAIN_ID=domain
            ):
                with self.assertRaises(ValueError):
                    self.source("time.sleep(60)\n")

    async def test_ros_domain_environment_is_pinned_at_source_startup(self):
        with patch.dict(os.environ, ROS_DOMAIN_ID="47"):
            source = self.source(
                "assert os.environ['ROS_DOMAIN_ID']=='47'\n"
                "m=json.dumps(dict(query_id=q,camera_id='sensor:webcam',"
                "availability='unavailable',"
                "reason='no_data',diagnostic='pinned domain',reset=False)).encode()\n"
                "os.write(1,struct.pack('!II',len(m),0)+m)\n"
            )
        with patch.dict(os.environ, ROS_DOMAIN_ID="93"):
            result = await source.read_latest()
        self.assertEqual(result.unavailable_reason, UnavailableReason.NO_DATA, result)
        self.assertEqual(result.diagnostic, "pinned domain")

    async def test_native_ros_metadata_cannot_claim_runtime_instance(self):
        jpeg = bytes.fromhex("ffd8ffc00011080001000203011100021100031100ffda0002ffd9")
        for instance in (None, "fabricated-runtime"):
            source = self.source(
                "clock='CLOCK_BOOTTIME:'+open('/proc/sys/kernel/random/boot_id')"
                ".read().strip()+':'"
                "+os.readlink('/proc/self/ns/time')\n"
                "m=dict(query_id=q,camera_id='sensor:webcam',"
                "availability='fresh',reason='',"
                "diagnostic='',reset=False,runtime_instance=" + repr(instance) + ","
                "receipt_boottime_ns=str(time.clock_gettime_ns(time.CLOCK_BOOTTIME)),"
                "clock_id=clock,"
                "source_stamp_sec=1,source_stamp_nanosec=2,"
                "sample_id='helper:1',width=2,height=1,"
                "source_width=2,source_height=1,source_encoding='rgb8',"
                "frame_id='target',"
                "publisher_gid='00'*24,topic='/camera/image',"
                "node_name='camera_publisher_node_12',"
                "node_namespace='/',ros_domain=0)\n"
                "m=json.dumps(m).encode(); b=" + repr(jpeg) + "\n"
                "os.write(1,struct.pack('!II',len(m),len(b))+m+b)\n"
                "sys.stdin.read()\n"
            )
            result = await source.read_latest()
            if instance is None:
                self.assertEqual(result.availability, Availability.FRESH, result)
                self.assertIsNone(result.runtime_instance)
            else:
                self.assertEqual(
                    result.unavailable_reason, UnavailableReason.INVALID_DATA
                )

    async def pending(self, source):
        task = asyncio.create_task(source.read_latest())
        for _ in range(100):
            if source._process is not None:
                return task, source._process
            await asyncio.sleep(0.01)
        self.fail("helper did not spawn")

    async def test_close_before_owned_query_starts_returns_closed(self):
        source = self.source("time.sleep(60)\n")
        consumer = asyncio.create_task(source.read_latest())
        await asyncio.sleep(0)
        self.assertIsNotNone(source._query)
        self.assertIsNone(source._process)
        self.assertIsNone(source._spawn_task)
        with patch("tools.mcp.image_source.asyncio.create_subprocess_exec") as spawn:
            await source.aclose()
            result = await consumer
            spawn.assert_not_called()
        self.assertEqual(result.unavailable_reason, UnavailableReason.CLOSED)
        self.assertFalse(consumer.cancelled())
        self.assertIsNone(source._query)
        self.assertIsNone(source._fd)

    async def test_gather_read_and_close_returns_closed_without_starting_helper(self):
        source = self.source("time.sleep(60)\n")
        with patch("tools.mcp.image_source.asyncio.create_subprocess_exec") as spawn:
            result, closed = await asyncio.gather(source.read_latest(), source.aclose())
            spawn.assert_not_called()
        self.assertEqual(result.unavailable_reason, UnavailableReason.CLOSED)
        self.assertIsNone(closed)
        self.assertIsNone(source._query)

    async def test_caller_cancel_before_query_start_propagates_during_close(self):
        source = self.source("time.sleep(60)\n")
        consumer = asyncio.create_task(source.read_latest())
        await asyncio.sleep(0)
        self.assertIsNotNone(source._query)
        self.assertIsNone(source._process)
        consumer.cancel()
        await source.aclose()
        with self.assertRaises(asyncio.CancelledError):
            await consumer
        self.assertTrue(consumer.cancelled())
        self.assertIsNone(source._process)

    async def test_caller_cancellation_during_close_still_propagates_after_reaping(
        self,
    ):
        source = self.source("time.sleep(60)\n")
        consumer, process = await self.pending(source)
        actual_wait = process.wait
        reaping = asyncio.Event()
        release = asyncio.Event()

        async def delayed_wait():
            reaping.set()
            await release.wait()
            return await actual_wait()

        process.wait = delayed_wait
        closing = asyncio.create_task(source.aclose())
        await reaping.wait()
        propagated = asyncio.Event()
        consumer.add_done_callback(lambda _: propagated.set())
        consumer.cancel()
        propagated_before_cleanup = False
        try:
            await asyncio.wait_for(propagated.wait(), 0.05)
            propagated_before_cleanup = True
        except asyncio.TimeoutError:
            pass
        finally:
            release.set()
            await closing
        self.assertFalse(
            propagated_before_cleanup,
            "Caller cancellation propagated before source cleanup completed",
        )
        with self.assertRaises(asyncio.CancelledError):
            await consumer
        self.assertTrue(consumer.cancelled())
        self.assertIsNotNone(process.returncode)

    async def timeout_cleanup(self, source):
        await source._start()
        process = source._process
        actual_wait = process.wait
        reaping = asyncio.Event()
        release = asyncio.Event()

        async def delayed_wait():
            reaping.set()
            await release.wait()
            return await actual_wait()

        process.wait = delayed_wait
        with patch("tools.mcp.image_source.READ_TIMEOUT", 0.05):
            consumer = asyncio.create_task(source.read_latest())
            await asyncio.wait_for(reaping.wait(), 1)
        return consumer, process, release

    async def test_close_during_timeout_cleanup_returns_closed_after_reaping(self):
        source = self.source("time.sleep(60)\n")
        consumer, process, release = await self.timeout_cleanup(source)
        completed = asyncio.Event()
        consumer.add_done_callback(lambda _: completed.set())
        closing = asyncio.create_task(source.aclose())
        completed_before_cleanup = False
        try:
            await asyncio.wait_for(completed.wait(), 0.05)
            completed_before_cleanup = True
        except asyncio.TimeoutError:
            pass
        finally:
            release.set()
            await closing
        self.assertFalse(
            completed_before_cleanup,
            "Closing during timeout cleanup completed the consumer before reaping",
        )
        result = await consumer
        self.assertEqual(result.unavailable_reason, UnavailableReason.CLOSED)
        self.assertFalse(consumer.cancelled())
        self.assertIsNotNone(process.returncode)

    async def test_caller_cancel_during_timeout_cleanup_waits_before_propagating(self):
        source = self.source("time.sleep(60)\n")
        consumer, process, release = await self.timeout_cleanup(source)
        completed = asyncio.Event()
        consumer.add_done_callback(lambda _: completed.set())
        consumer.cancel()
        completed_before_cleanup = False
        try:
            await asyncio.wait_for(completed.wait(), 0.05)
            completed_before_cleanup = True
        except asyncio.TimeoutError:
            pass
        finally:
            release.set()
        self.assertFalse(
            completed_before_cleanup,
            "Caller cancellation during timeout cleanup propagated before reaping",
        )
        with self.assertRaises(asyncio.CancelledError):
            await consumer
        self.assertTrue(consumer.cancelled())
        self.assertIsNotNone(process.returncode)

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

    async def test_cancellation_with_backpressured_stdout_allows_next_query(self):
        source = self.source("os.write(1,b'x'*262144)\ntime.sleep(60)\n")
        await source._start()
        process = source._process
        original = process.stdout.readexactly
        release = asyncio.Event()

        async def delayed_read(size):
            await release.wait()
            return await original(size)

        # Hold the consumer while a real child fills stdout past its pause
        # threshold. This deterministically exercises cancellation under pipe
        # backpressure, independent of JPEG contents or scheduler timing.
        process.stdout.readexactly = delayed_read
        actual_kill = process.kill

        def delayed_exit():
            # Reproduce wait() starting before the child's asynchronous exit
            # notification, rather than its already-exited fast path.
            asyncio.get_running_loop().call_later(0.05, actual_kill)

        process.kill = delayed_exit
        task = asyncio.create_task(source.read_latest())
        try:
            deadline = asyncio.get_running_loop().time() + 5
            while not process.stdout._paused:
                self.assertLess(asyncio.get_running_loop().time(), deadline)
                await asyncio.sleep(0.01)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
            self.assertIsNotNone(process.returncode)
            self.assertFalse(source._poisoned)
            self.assertIsNone(source._process)
            self.assertTrue(
                process.stdout.at_eof(),
                "Cleanup left a paused stdout transport and buffered output behind",
            )
        finally:
            release.set()
            process.kill = actual_kill
            process.stdout.readexactly = original
            if not task.done():
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)
            if process.returncode is None:
                process.kill()
            await asyncio.wait_for(process.stdout.read(), 1)
            await asyncio.wait_for(process.wait(), 1)

        Path(source._executable).write_text(
            "#!/usr/bin/python3\nimport sys,json,struct,os\n"
            "q=int(sys.stdin.readline())\n"
            "m=json.dumps(dict(query_id=q,camera_id='sensor:webcam',"
            "availability='unavailable',reason='no_data',"
            "diagnostic='recovered',reset=False)).encode()\n"
            "os.write(1,struct.pack('!II',len(m),0)+m)\n"
        )
        result = await source.read_latest()
        self.assertEqual(result.unavailable_reason, UnavailableReason.NO_DATA)
        self.assertEqual(result.diagnostic, "recovered")

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
