"""No hardware: retention, ownership, low-space pause/resume, and hook safety."""

import json
import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from ros2.utils.media_recording import (
    Recorder,
    RecordingStore,
    finalize_clip,
    mark_complete,
    recorder_config,
    validate_recording,
)


def options(directory):
    return SimpleNamespace(
        recording_directory=str(directory),
        recording_segment_sec=120,
        recording_reserve_percent=20,
        recording_camera_name="ircam",
        recording_timezone="America/Los_Angeles",
        bitrate_kbps=1500,
        rtsp_port=8554,
        stream_name="baby",
    )


class RecordingTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "recordings"
        self.store = RecordingStore(self.root, 20, 50)
        self.addCleanup(self.store.close)

    def clip(self, sequence, completed=True, run=None):
        run = run or self.root / ("run-" + "a" * 32)
        run.mkdir(exist_ok=True)
        path = run / f"recording-20261017T120000-{sequence:06}.mp4"
        path.write_bytes(b"x" * 100)
        os.utime(path, (sequence, sequence))
        if completed:
            path.with_suffix(".complete").touch()
        return path

    def disk(self):
        # 1000 total, 650 occupied by OS/other data: recording must share the disk.
        occupied = sum(p.stat().st_size for p in self.store.clips())
        return SimpleNamespace(f_blocks=1000, f_bavail=350 - occupied, f_frsize=1)

    def test_oldest_first_counts_other_data_and_protects_active(self):
        oldest = self.clip(1)
        newer = self.clip(2)
        active = self.clip(3, completed=False)
        self.store.current = active.parent
        foreign = self.root / "family.mp4"
        foreign.write_bytes(b"keep")
        with patch("os.statvfs", side_effect=lambda _: self.disk()):
            ready, available, reserve = self.store.prune()
        self.assertTrue(ready)
        self.assertEqual((available, reserve), (250, 200))
        self.assertFalse(oldest.exists())
        self.assertFalse(newer.exists())
        self.assertTrue(active.exists())
        self.assertEqual(foreign.read_bytes(), b"keep")

    def test_only_oldest_needed_is_deleted(self):
        oldest, newest = self.clip(1), self.clip(2)
        with patch("os.statvfs", side_effect=lambda _: self.disk()):
            self.assertTrue(self.store.prune()[0])
        self.assertFalse(oldest.exists())
        self.assertTrue(newest.exists())

    def test_interrupted_previous_session_expires_but_active_does_not(self):
        interrupted = self.clip(1, completed=False)
        self.store.current = interrupted.parent
        with patch.object(self.store, "space", return_value=(0, 200)):
            self.assertFalse(self.store.prune()[0])
            self.assertTrue(interrupted.exists())
            self.store.current = None
            self.store.prune()
        self.assertFalse(interrupted.exists())

    def test_rejects_unmanaged_directories_and_parallel_owner(self):
        with self.assertRaises(BlockingIOError):
            RecordingStore(self.root, 20, 50)
        other = Path(self.temp.name) / "photos"
        other.mkdir()
        (other / "photo.mp4").touch()
        with self.assertRaises(ValueError):
            RecordingStore(other, 20, 50)
        alias = Path(self.temp.name) / "alias"
        alias.symlink_to(self.root)
        with self.assertRaises(ValueError):
            RecordingStore(alias, 20, 50)

    def test_shared_store_prunes_oldest_across_streams_and_keeps_open_clips(self):
        peer = RecordingStore(self.root, 20, 50, "webcam")
        self.addCleanup(peer.close)
        primary_run = self.store.begin_session()
        secondary_run = peer.begin_session()
        oldest = self.clip(1, run=primary_run)
        newer = self.clip(2, run=secondary_run)
        primary_open = self.clip(3, completed=False, run=primary_run)
        secondary_open = self.clip(4, completed=False, run=secondary_run)

        def disk(_):
            occupied = sum(p.stat().st_size for p in self.store.clips())
            return SimpleNamespace(f_blocks=1000, f_bavail=550 - occupied, f_frsize=1)

        # The webcam's cleanup must reclaim the older IR clip, not its newer clip.
        with patch("os.statvfs", side_effect=disk):
            self.assertTrue(peer.prune()[0])
        self.assertFalse(oldest.exists())
        self.assertTrue(newer.exists())
        with patch.object(peer, "space", return_value=(0, 200)):
            self.assertFalse(peer.prune()[0])
        self.assertTrue(primary_open.exists())
        self.assertTrue(secondary_open.exists())

    def test_orphan_child_protects_session_until_it_exits(self):
        peer = RecordingStore(self.root, 20, 50, "webcam")
        self.addCleanup(peer.close)
        session = self.store.begin_session()
        active = self.clip(1, completed=False, run=session)
        child = subprocess.Popen(
            [sys.executable, "-c", "import sys; sys.stdin.read()"],
            stdin=subprocess.PIPE,
            pass_fds=(self.store.lock, self.store.stream_lock, self.store.session_lock),
        )
        try:
            self.store.close()  # Simulate parent death; child still owns the clip.
            with self.assertRaises(BlockingIOError):
                RecordingStore(self.root, 20, 50)
            with patch.object(peer, "space", return_value=(0, 200)):
                peer.prune()
                self.assertTrue(active.exists())
                child.communicate(timeout=5)
                peer.prune()
            self.assertFalse(active.exists())
        finally:
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)

    def test_completion_hook_publishes_flat_local_name_after_close(self):
        session = self.store.begin_session()
        active = self.clip(1, completed=False, run=session)
        self.assertFalse(list(self.root.glob("*.mp4")))
        with patch.dict(os.environ, {"MTX_SEGMENT_PATH": str(active)}):
            command = recorder_config(options(self.root), session)["paths"][
                "recording"
            ]["runOnRecordSegmentComplete"]
            subprocess.run(shlex.split(command), check=True, timeout=10)
        self.assertFalse(active.exists())
        saved = self.root / "202610170500_ircam.mp4"
        self.assertEqual(saved.read_bytes(), b"x" * 100)
        self.assertEqual(saved.stat().st_mtime, 1)
        self.assertEqual(list(self.root.glob("*.mp4")), [saved])

    def test_local_dates_and_dst_collisions_never_overwrite(self):
        session = self.store.begin_session()
        for stamp, expected in (
            ("20261116T051800", "202611152118_ircam.mp4"),
            ("20260716T041800", "202607152118_ircam.mp4"),
            ("20261101T083000", "202611010130_ircam.mp4"),
            ("20261101T093000", "202611010130_ircam__2.mp4"),
            ("20261101T093010", "202611010130_ircam__3.mp4"),
        ):
            clip = session / f"recording-{stamp}-000000.mp4"
            clip.write_text(stamp)
            with self.store.retention_lock():
                saved = finalize_clip(clip, self.root, "ircam", "America/Los_Angeles")
            self.assertEqual(saved.name, expected)
            self.assertEqual(saved.read_text(), stamp)
        self.assertEqual(
            (self.root / "202611010130_ircam.mp4").read_text(), "20261101T083000"
        )

    def test_flat_retention_is_oldest_first_and_keeps_both_open_streams(self):
        peer = RecordingStore(self.root, 20, 50, "webcam")
        self.addCleanup(peer.close)
        primary = self.clip(1, completed=False, run=self.store.begin_session())
        secondary = self.clip(2, completed=False, run=peer.begin_session())
        oldest = self.root / "202611152118_ircam.mp4"
        oldest.write_bytes(b"x" * 100)
        os.utime(oldest, (1, 1))
        newest = self.root / "202611152120_webcam.mp4"
        newest.write_bytes(b"x" * 100)
        os.utime(newest, (2, 2))
        with patch("os.statvfs", side_effect=lambda _: self.disk()):
            self.assertFalse(peer.prune()[0])
        self.assertFalse(oldest.exists())
        self.assertFalse(newest.exists())
        self.assertTrue(primary.exists())
        self.assertTrue(secondary.exists())

    def test_symlinks_and_foreign_files_are_preserved(self):
        external = Path(self.temp.name) / "external"
        external.mkdir()
        target = self.clip(1, run=external)
        session = self.root / ("run-" + "b" * 32)
        session.mkdir()
        link = session / target.name
        link.symlink_to(target)
        (self.root / ("run-" + "c" * 32)).symlink_to(external)
        foreign = session / "unrelated.mp4"
        foreign.touch()
        with patch.object(self.store, "space", return_value=(0, 200)):
            self.store.prune()
        self.assertTrue(target.exists())
        self.assertTrue(link.is_symlink())
        self.assertTrue(foreign.exists())

    def test_completion_hook_and_write_failure(self):
        active = self.clip(1, completed=False, run=self.store.begin_session())
        with patch.dict(os.environ, {"MTX_SEGMENT_PATH": str(active)}):
            mark_complete(str(self.root), "ircam", "America/Los_Angeles")
        saved = self.root / "202610170500_ircam.mp4"
        self.assertTrue(saved.exists())
        with patch.object(self.store, "space", return_value=(0, 200)):
            with patch.object(Path, "unlink", side_effect=PermissionError("read-only")):
                with self.assertRaises(PermissionError):
                    self.store.prune()
        self.assertTrue(saved.exists())

    def test_hook_rejects_foreign_and_symlink_paths(self):
        active = self.clip(1, completed=False, run=self.store.begin_session())
        foreign = self.root / active.name
        foreign.write_bytes(b"keep")
        for clip in (foreign, self.root / "missing.mp4"):
            with patch.dict(os.environ, {"MTX_SEGMENT_PATH": str(clip)}):
                with self.assertRaises(ValueError):
                    mark_complete(str(self.root), "ircam", "UTC")
        link = active.with_name("recording-20261017T120000-000002.mp4")
        link.symlink_to(foreign)
        with self.assertRaises(ValueError):
            finalize_clip(link, self.root, "ircam", "UTC")
        self.assertEqual(foreign.read_bytes(), b"keep")

    def test_recording_config_is_headless_encoded_stream_copy(self):
        config = recorder_config(options(self.root), self.root / ("run-" + "a" * 32))
        for name in ("rtsp", "rtmp", "hls", "webrtc", "srt", "moq", "api", "playback"):
            self.assertFalse(config[name])
        path = config["paths"]["recording"]
        self.assertEqual(path["source"], "rtsp://127.0.0.1:8554/baby")
        self.assertEqual(path["recordSegmentDuration"], "120s")
        self.assertEqual(path["recordDeleteAfter"], "0s")

    def test_invalid_options(self):
        for field, value in (
            ("recording_directory", "relative"),
            ("recording_directory", "/data/%path"),
            ("recording_segment_sec", 1),
            ("recording_reserve_percent", 19),
            ("recording_camera_name", "../ircam"),
            ("recording_camera_name", "x" * 65),
            ("recording_timezone", "Unknown/Timezone"),
        ):
            config = options(self.root)
            setattr(config, field, value)
            with self.assertRaises((ValueError, KeyError)):
                validate_recording(config)

    def test_low_space_stops_only_recorder_and_resumes_after_reclaim(self):
        recorder = Recorder(options(Path(self.temp.name) / "second"), "unused")
        self.addCleanup(recorder.close)
        process = Mock()
        process.poll.return_value = None
        recorder.process = process
        with patch.object(recorder.store, "prune", return_value=(False, 100, 200)):
            recorder.tick()
        process.terminate.assert_called_once()
        process.wait.assert_called_once()
        self.assertIsNone(recorder.process)
        self.assertEqual(recorder.status["state"], "paused_low_space")
        status = json.loads((recorder.store.staging / "status-ircam.json").read_text())
        self.assertEqual(status["reserve_percent"], 20)
        with patch.object(recorder.store, "prune", return_value=(True, 400, 200)):
            with patch("subprocess.Popen", return_value=process) as launch:
                recorder.tick()
        launch.assert_called_once()
        self.assertEqual(recorder.status["state"], "waiting_for_stream")
        recorder.stop_process()

    def test_segment_publication_gap_does_not_restart_recorder(self):
        recorder = Recorder(options(Path(self.temp.name) / "gap"), "unused")
        self.addCleanup(recorder.close)
        process = Mock()
        process.poll.return_value = None
        with patch("subprocess.Popen", return_value=process):
            with patch("time.monotonic", return_value=100):
                recorder.tick()
            active = self.clip(1, completed=False, run=recorder.store.current)
            with patch("time.monotonic", return_value=130):
                with patch("time.time", return_value=1):
                    recorder.tick()
            with patch.dict(os.environ, {"MTX_SEGMENT_PATH": str(active)}):
                mark_complete(str(recorder.store.root), "ircam", "UTC")
            with patch("time.monotonic", return_value=132):
                recorder.tick()
        self.assertEqual(recorder.status["state"], "recording")
        process.terminate.assert_not_called()


if __name__ == "__main__":
    unittest.main()
