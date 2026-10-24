#!/usr/bin/env python3
import sys
import time
import threading
import collections
from typing import Deque, Tuple

import numpy as np
import cv2

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import PointCloud2
# Avoid sensor_msgs_py dependency; parse PointCloud2 buffer directly


HISTORY_WINDOW_S = 0.4  # seconds to retain for fading
PLOT_LIMIT = 4.0        # +/- meters on each axis
IMG_SIZE = 800          # pixels (square)


def extract_xy_from_cloud2(msg: PointCloud2):
    # Build dtype for x,y with correct endianness and offsets, respecting row padding
    x_field = None
    y_field = None
    for f in msg.fields:
        if f.name == 'x':
            x_field = f
        elif f.name == 'y':
            y_field = f
    if x_field is None or y_field is None:
        return np.array([], dtype=np.float32), np.array([], dtype=np.float32)

    # Only FLOAT32 supported here
    FLOAT32 = 7
    if x_field.datatype != FLOAT32 or y_field.datatype != FLOAT32:
        raise ValueError('Non-float32 x/y not supported')

    endian = '<f4' if not msg.is_bigendian else '>f4'
    point_dtype = np.dtype({
        'names': ['x', 'y'],
        'formats': [endian, endian],
        'offsets': [x_field.offset, y_field.offset],
        'itemsize': msg.point_step,
    })

    # Respect row padding via explicit strides
    try:
        arr2d = np.ndarray(
            shape=(msg.height if msg.height > 0 else 1, msg.width),
            dtype=point_dtype,
            buffer=msg.data,
            strides=(msg.row_step if msg.row_step > 0 else msg.width * msg.point_step, msg.point_step),
        )
        flat = arr2d.reshape(-1)
    except Exception:
        # Fallback: assume tightly packed
        flat = np.frombuffer(msg.data, dtype=point_dtype, count=msg.width * max(1, msg.height))

    xs = flat['x'].astype(np.float32, copy=False)
    ys = flat['y'].astype(np.float32, copy=False)
    mask = np.isfinite(xs) & np.isfinite(ys)
    return xs[mask], ys[mask]


class LidarCloudSubscriber(Node):
    def __init__(self, topic: str):
        super().__init__('pointcloud2_visualizer')
        self.topic = topic
        self.scan_history: Deque[Tuple[float, np.ndarray, np.ndarray]] = collections.deque()
        self.lock = threading.Lock()
        self.sub = self.create_subscription(PointCloud2, topic, self._callback, 10)
        self.get_logger().info(f"Subscribed to PointCloud2 topic: {topic}")

    def _callback(self, msg: PointCloud2):
        try:
            xs_np, ys_np = extract_xy_from_cloud2(msg)
            if xs_np.size == 0:
                return
        except Exception as e:
            self.get_logger().warn(f"Failed to parse PointCloud2: {e}")
            return

        now = time.time()
        with self.lock:
            self.scan_history.append((now, xs_np, ys_np))
            while len(self.scan_history) > 200:
                self.scan_history.popleft()

    def pop_history(self):
        now = time.time()
        with self.lock:
            while self.scan_history and (now - self.scan_history[0][0]) > HISTORY_WINDOW_S:
                self.scan_history.popleft()
            return list(self.scan_history)


def world_to_image(x: np.ndarray, y: np.ndarray):
    s = IMG_SIZE / (2.0 * PLOT_LIMIT)
    cx = IMG_SIZE // 2
    cy = IMG_SIZE // 2
    px = (cx + (x * s)).astype(np.int32)
    py = (cy - (y * s)).astype(np.int32)
    return px, py


def run_visualizer(topic: str):
    rclpy.init(args=None)
    node = LidarCloudSubscriber(topic)

    spin_thread = threading.Thread(target=rclpy.spin, args=(node,), daemon=True)
    spin_thread.start()

    cv2.namedWindow('PointCloud2 XY', cv2.WINDOW_NORMAL)
    cv2.resizeWindow('PointCloud2 XY', IMG_SIZE, IMG_SIZE)

    last_fps_time = time.time()
    frames = 0

    try:
        while rclpy.ok():
            canvas = np.zeros((IMG_SIZE, IMG_SIZE, 3), dtype=np.uint8)

            # axes
            cv2.line(canvas, (0, IMG_SIZE//2), (IMG_SIZE, IMG_SIZE//2), (60, 60, 60), 1)
            cv2.line(canvas, (IMG_SIZE//2, 0), (IMG_SIZE//2, IMG_SIZE), (60, 60, 60), 1)

            history = node.pop_history()
            now = time.time()
            scans = 0
            points_drawn = 0
            for ts, xs, ys in history:
                age = now - ts
                age = 0.0 if age < 0 else age
                # Bright when new, dim when old
                fade = max(0.0, 1.0 - (age / HISTORY_WINDOW_S))
                color = (0, int(255 * fade), int(255 * (0.3 + 0.7 * fade)))  # BGR

                px, py = world_to_image(xs, ys)
                # Clip to canvas
                mask = (px >= 0) & (px < IMG_SIZE) & (py >= 0) & (py < IMG_SIZE)
                px = px[mask]
                py = py[mask]
                for (ix, iy) in zip(px, py):
                    cv2.circle(canvas, (int(ix), int(iy)), 2, color, -1)
                scans += 1
                points_drawn += len(px)

            # lidar center
            cv2.circle(canvas, (IMG_SIZE//2, IMG_SIZE//2), 5, (255, 255, 0), -1)

            frames += 1
            if now - last_fps_time >= 1.0:
                fps = frames / (now - last_fps_time)
                last_fps_time = now
                frames = 0
                node.get_logger().debug(f"FPS: {fps:.1f}")

            title = f"PointCloud2 XY - scans:{scans} points:{points_drawn} topic:{topic}"
            cv2.setWindowTitle('PointCloud2 XY', title)
            cv2.imshow('PointCloud2 XY', canvas)

            key = cv2.waitKey(20)
            if key == 27 or key == ord('q'):
                break

    finally:
        try:
            node.get_logger().info("Shutting down visualizer...")
        except Exception:
            pass
        rclpy.shutdown()
        cv2.destroyAllWindows()


def main():
    topic = sys.argv[1] if len(sys.argv) > 1 else "/point_cloud"
    run_visualizer(topic)


if __name__ == "__main__":
    main()
