"""EfficientDet Lite0 object detection plus a simple whole-frame motion score."""

import zipfile
from pathlib import Path

import numpy as np
from ai_edge_litert.interpreter import Interpreter
from PIL import Image


def decode_image(message):
    if message.encoding not in ("rgb8", "bgr8"):
        raise ValueError("Expected rgb8/bgr8 camera image")
    if not 0 < message.width <= 4096 or not 0 < message.height <= 4096:
        raise ValueError("Invalid image dimensions")
    if (
        message.step < message.width * 3
        or len(message.data) != message.height * message.step
    ):
        raise ValueError("Invalid image stride/payload")
    rows = np.frombuffer(bytes(message.data), dtype=np.uint8).reshape(
        message.height, message.step
    )
    rgb = rows[:, : message.width * 3].reshape(message.height, message.width, 3)
    return rgb[:, :, ::-1].copy() if message.encoding == "bgr8" else rgb.copy()


class VideoDetector:
    def __init__(self, model_path="", num_threads=2):
        path = (
            Path(model_path)
            if model_path
            else Path(__file__).with_name("efficientdet_lite0.tflite")
        )
        with zipfile.ZipFile(path) as archive:
            self.labels = archive.read("labelmap.txt").decode().splitlines()
        self.interpreter = Interpreter(model_path=str(path), num_threads=num_threads)
        self.interpreter.allocate_tensors()
        self.input = self.interpreter.get_input_details()[0]
        self.outputs = self.interpreter.get_output_details()
        if (
            list(self.input["shape"]) != [1, 320, 320, 3]
            or self.input["dtype"] != np.uint8
        ):
            raise ValueError("Expected uint8 EfficientDet Lite0 input")
        if [list(x["shape"]) for x in self.outputs] != [
            [1, 25, 4],
            [1, 25],
            [1, 25],
            [1],
        ]:
            raise ValueError("Unsupported EfficientDet output contract")
        self.previous = None

    def infer(self, rgb, threshold):
        image = Image.fromarray(rgb)
        pixels = np.asarray(image.resize((320, 320)), dtype=np.uint8)[None, ...]
        self.interpreter.set_tensor(self.input["index"], pixels)
        self.interpreter.invoke()
        boxes, classes, scores, count = [
            self.interpreter.get_tensor(x["index"])[0] for x in self.outputs
        ]
        detections = []
        for i in range(min(int(count), len(scores))):
            category = int(classes[i])
            if float(scores[i]) >= threshold and 0 <= category < len(self.labels):
                detections.append(
                    {
                        "label": self.labels[category],
                        "score": float(scores[i]),
                        "box_yxyx": boxes[i].tolist(),
                    }
                )
        gray = np.asarray(image.convert("L").resize((160, 120)), dtype=np.int16)
        motion = (
            0.0
            if self.previous is None
            else float(np.mean(np.abs(gray - self.previous) > 25))
        )
        self.previous = gray
        return detections, motion
