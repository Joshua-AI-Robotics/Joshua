import unittest
from types import SimpleNamespace

import numpy as np
from robot.perception.proto.perception_packet_pb2 import PerceptionPacket

from ai.models.video_detection.processor import VideoDetector, decode_image
from ai.models.yamnet.processor import AudioWindow, Yamnet


def packet(sequence, frames=8000, channels=1, stamp=None):
    result = PerceptionPacket(
        perception_id="test",
        sequence_id=sequence,
        timestamp_ns=(
            stamp if stamp is not None else 1_000_000_000 + sequence * 500_000_000
        ),
    )
    result.audio.sample_rate_hz = 16000
    result.audio.channels = channels
    result.audio.frames = frames
    result.audio.encoding = "s16le"
    result.audio.data = np.full(frames * channels, 16384, dtype="<i2").tobytes()
    return result


class ProcessorTest(unittest.TestCase):
    def test_windows_downmix_and_reset_on_loss(self):
        window = AudioWindow()
        self.assertTrue(window.append(packet(0, channels=2)))
        self.assertIsNone(window.take())
        self.assertFalse(window.append(packet(1, channels=2)))
        np.testing.assert_allclose(window.take(), 0.5)
        self.assertIsNone(window.take())
        self.assertTrue(window.append(packet(3)))
        self.assertIsNone(window.take())
        bad = packet(4)
        bad.audio.data = b"x"
        with self.assertRaises(ValueError):
            window.append(bad)
        self.assertEqual(len(window.samples), 0)

    def test_restart_and_timestamp_discontinuity_clear_window(self):
        window = AudioWindow()
        window.append(packet(0))
        window.append(packet(1))
        restart = packet(0)
        restart.audio.discontinuity = True
        self.assertTrue(window.append(restart))
        self.assertIsNone(window.take())
        self.assertTrue(window.append(packet(1, stamp=9_000_000_000)))
        self.assertIsNone(window.take())

    def test_image_stride_and_bgr(self):
        image = SimpleNamespace(
            width=1,
            height=2,
            step=4,
            encoding="bgr8",
            data=bytes([1, 2, 3, 0, 4, 5, 6, 0]),
        )
        np.testing.assert_array_equal(decode_image(image), [[[3, 2, 1]], [[6, 5, 4]]])
        image.data = b"x"
        with self.assertRaises(ValueError):
            decode_image(image)

    def test_bundled_models_execute_without_hardware(self):
        audio = Yamnet()
        score, labels = audio.infer(np.zeros(15600, dtype=np.float32))
        self.assertTrue(0 <= score <= 1)
        self.assertEqual(len(labels), 3)
        video = VideoDetector()
        _, motion = video.infer(np.zeros((48, 64, 3), dtype=np.uint8), 0.5)
        self.assertEqual(motion, 0)
        _, motion = video.infer(np.full((48, 64, 3), 255, dtype=np.uint8), 0.5)
        self.assertEqual(motion, 1)


if __name__ == "__main__":
    unittest.main()
