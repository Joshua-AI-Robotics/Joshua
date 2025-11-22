"""
Image conversion utilities for ROS2 that work with any NumPy version.

This module provides image conversion functions as an alternative to cv_bridge,
compatible with both NumPy 1.x and 2.x.
"""

import numpy as np
from sensor_msgs.msg import Image


def imgmsg_to_numpy(msg: Image, desired_encoding: str = 'passthrough') -> np.ndarray:
    """
    Convert a ROS Image message to a NumPy array.
    
    Args:
        msg: ROS Image message
        desired_encoding: Target encoding ('rgb8', 'bgr8', 'mono8', or 'passthrough')
    
    Returns:
        NumPy array containing the image data
    """
    dtype_map = {
        'uint8': np.uint8,
        'int8': np.int8,
        'uint16': np.uint16,
        'int16': np.int16,
        'uint32': np.uint32,
        'int32': np.int32,
        'float32': np.float32,
        'float64': np.float64,
    }
    
    # Determine the number of channels
    channels = 1
    if msg.encoding in ['rgb8', 'bgr8']:
        channels = 3
    elif msg.encoding in ['rgba8', 'bgra8']:
        channels = 4
    elif msg.encoding in ['mono8', 'mono16']:
        channels = 1
    elif msg.encoding in ['16UC1', '32FC1']:
        channels = 1
    
    # Determine dtype
    if msg.encoding in ['rgb8', 'bgr8', 'rgba8', 'bgra8', 'mono8']:
        dtype = np.uint8
    elif msg.encoding in ['mono16', '16UC1']:
        dtype = np.uint16
    elif msg.encoding in ['32FC1']:
        dtype = np.float32
    else:
        dtype = np.uint8  # Default
    
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


