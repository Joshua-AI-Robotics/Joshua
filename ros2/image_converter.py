"""
Image conversion utilities for ROS2 that work with any NumPy version.

This module provides image conversion functions as an alternative to cv_bridge,
compatible with both NumPy 1.x and 2.x.
"""

import numpy as np
from sensor_msgs.msg import Image


_ENCODING_MAP = {
    # Encoding: (channels, dtype, bytes_per_channel)
    'rgb8': (3, np.uint8),
    'bgr8': (3, np.uint8),
    'rgba8': (4, np.uint8),
    'bgra8': (4, np.uint8),
    'mono8': (1, np.uint8),
    'mono16': (1, np.uint16),
    '16UC1': (1, np.uint16),
    '32FC1': (1, np.float32),
}


def imgmsg_to_numpy(msg: Image, desired_encoding: str = 'passthrough') -> np.ndarray:
    """
    Convert a ROS Image message to a NumPy array.

    Args:
        msg: ROS Image message
        desired_encoding: Target encoding ('rgb8', 'bgr8', 'mono8', or 'passthrough')

    Returns:
        NumPy array containing the image data
    """

    channels, dtype = _ENCODING_MAP.get(msg.encoding, (1, np.uint8))
    
    # Convert bytes to numpy array
    img_array = np.frombuffer(msg.data, dtype=dtype)

    # Reshape to image dimensions
    if channels > 1:
        img_array = img_array.reshape(msg.height, msg.width, channels)
    else:
        img_array = img_array.reshape(msg.height, msg.width)

    # Handle encoding conversion
    if desired_encoding != 'passthrough' and desired_encoding != msg.encoding:
        if msg.encoding == 'bgr8' and desired_encoding == 'rgb8':
            # BGR to RGB
            img_array = img_array[:, :, ::-1].copy()
        elif msg.encoding == 'rgb8' and desired_encoding == 'bgr8':
            # RGB to BGR
            img_array = img_array[:, :, ::-1].copy()

    return img_array


def numpy_to_imgmsg(img_array: np.ndarray, encoding: str = 'rgb8') -> Image:
    """
    Convert a NumPy array to a ROS Image message.

    Args:
        img_array: NumPy array containing image data
        encoding: Image encoding ('rgb8', 'bgr8', 'mono8', etc.)

    Returns:
        ROS Image message
    """
    msg = Image()

    # Set dimensions
    if len(img_array.shape) == 2:
        # Grayscale
        msg.height, msg.width = img_array.shape
        msg.encoding = 'mono8' if encoding == 'passthrough' else encoding
        channels = 1
    elif len(img_array.shape) == 3:
        # Color
        msg.height, msg.width, channels = img_array.shape
        msg.encoding = encoding
    else:
        raise ValueError(f"Invalid image shape: {img_array.shape}")

    # Set step (bytes per row)
    msg.step = msg.width * channels * img_array.dtype.itemsize

    # Set data
    msg.data = img_array.tobytes()

    # Set header timestamp (caller should set frame_id if needed)
    msg.header.stamp.sec = 0
    msg.header.stamp.nanosec = 0

    return msg


class ImageConverter:
    """
    Simple image converter class that mimics cv_bridge.CvBridge interface.
    Compatible with any NumPy version.
    """

    def imgmsg_to_cv2(self, msg: Image, desired_encoding: str = 'passthrough') -> np.ndarray:
        """
        Convert ROS Image message to OpenCV format (NumPy array).

        Args:
            msg: ROS Image message
            desired_encoding: Target encoding

        Returns:
            NumPy array in OpenCV format (BGR by default)
        """
        return imgmsg_to_numpy(msg, desired_encoding)

    def cv2_to_imgmsg(self, img_array: np.ndarray, encoding: str = 'bgr8') -> Image:
        """
        Convert OpenCV image (NumPy array) to ROS Image message.

        Args:
            img_array: NumPy array containing image data
            encoding: Image encoding

        Returns:
            ROS Image message
        """
        return numpy_to_imgmsg(img_array, encoding)


