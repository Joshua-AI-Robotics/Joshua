"""No hardware: retention, ownership, low-space pause/resume, and hook safety."""

import json
import os
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from ros2.utils.media_recording import (
    Recorder,
    RecordingStore,
    mark_complete,
    recorder_config,
    validate_recording,
)


def options(directory):
    return SimpleNamespace(
        recording_directory=str(directory),
        recording_segment_sec=120,
        recording_reserve_percent=20,
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
        active = self.clip(1, completed=False)
        self.store.current = active.parent
        with patch.dict(os.environ, {"MTX_SEGMENT_PATH": str(active)}):
            mark_complete()
        self.assertTrue(active.with_suffix(".complete").exists())
        with patch.object(self.store, "space", return_value=(0, 200)):
            with patch.object(Path, "unlink", side_effect=PermissionError("read-only")):
                with self.assertRaises(PermissionError):
                    self.store.prune()
        self.assertTrue(active.exists())

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
        ):
            config = options(self.root)
            setattr(config, field, value)
            with self.assertRaises(ValueError):
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
        status = json.loads((recorder.store.root / "status.json").read_text())
        self.assertEqual(status["reserve_percent"], 20)
        with patch.object(recorder.store, "prune", return_value=(True, 400, 200)):
            with patch("subprocess.Popen", return_value=process) as launch:
                recorder.tick()
        launch.assert_called_once()
        self.assertEqual(recorder.status["state"], "waiting_for_stream")
        recorder.stop_process()


if __name__ == "__main__":
    unittest.main()
