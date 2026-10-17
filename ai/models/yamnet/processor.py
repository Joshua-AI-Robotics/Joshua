"""ROS-free YAMNet inference and contiguous PCM window assembly."""

import zipfile
from pathlib import Path

import numpy as np
from ai_edge_litert.interpreter import Interpreter


class AudioWindow:
    def __init__(self, size=15600):
        self.size = size
        self.reset()

    def reset(self):
        self.samples = np.empty(0, dtype=np.float32)
        self.sequence = None
        self.source = None
        self.end_ns = None
        self.new_samples = 0

    def append(self, packet):
        audio = packet.audio
        if (
            packet.WhichOneof("data_type") != "audio"
            or audio.encoding != "s16le"
            or audio.sample_rate_hz != 16000
            or not 1 <= audio.channels <= 8
            or not 1 <= audio.frames <= 16000
            or len(audio.data) != audio.frames * audio.channels * 2
        ):
            self.reset()
            raise ValueError("Expected bounded 16 kHz interleaved s16le AudioData")
        gap = (
            audio.discontinuity
            or self.sequence is None
            or packet.sequence_id != self.sequence + 1
            or packet.perception_id != self.source
            or abs(packet.timestamp_ns - self.end_ns) > 100_000_000
        )
        if gap:
            self.reset()
        values = np.frombuffer(audio.data, dtype="<i2").reshape(-1, audio.channels)
        mono = values.astype(np.float32).mean(axis=1) / 32768.0
        self.samples = np.concatenate((self.samples, mono))[-self.size :]
        self.new_samples += len(mono)
        self.sequence = packet.sequence_id
        self.source = packet.perception_id
        self.end_ns = packet.timestamp_ns + audio.frames * 1_000_000_000 // 16000
        return gap

    def take(self):
        if len(self.samples) < self.size or self.new_samples < self.size // 2:
            return None
        self.new_samples = 0
        return self.samples.copy()


class Yamnet:
    def __init__(self, model_path="", num_threads=2):
        path = (
            Path(model_path)
            if model_path
            else Path(__file__).with_name("yamnet.tflite")
        )
        with zipfile.ZipFile(path) as archive:
            self.labels = archive.read("yamnet_label_list.txt").decode().splitlines()
        self.interpreter = Interpreter(model_path=str(path), num_threads=num_threads)
        self.interpreter.allocate_tensors()
        self.input = self.interpreter.get_input_details()[0]
        self.output = self.interpreter.get_output_details()[0]
        if list(self.input["shape"]) != [15600] or self.input["dtype"] != np.float32:
            raise ValueError("Expected the waveform-input YAMNet TFLite model")
        self.cry_index = self.labels.index("Baby cry, infant cry")

    def infer(self, waveform):
        self.interpreter.set_tensor(self.input["index"], waveform)
        self.interpreter.invoke()
        scores = self.interpreter.get_tensor(self.output["index"])[0]
        if len(scores) != len(self.labels) or not np.isfinite(scores).all():
            raise ValueError("Invalid YAMNet scores")
        top = np.argsort(scores)[-3:][::-1]
        return float(scores[self.cry_index]), [
            {"label": self.labels[i], "score": float(scores[i])} for i in top
        ]
