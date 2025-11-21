"""Real-time data store for ROS2 messages in HuggingFace format."""

import os
import time
import json
from typing import Any, Dict, List
from datasets import Dataset
from datetime import datetime
import glog

import rclpy
from rclpy.serialization import serialize_message
from rosidl_runtime_py.utilities import get_message
import rosbag2_py

from ai.proto import data_store_pb2
from ros2.ros2_type_resolver import get_ros2_type_name

class DataStore:
    """Real-time data store for ROS2 messages using rosbag2 and HuggingFace format."""
    
    def __init__(self, data_store_config: data_store_pb2.DataStore):
        """Initialize the data store.
        
        Args:
            data_store_config: data_store.proto.
        """
        self.data_store_type = data_store_config.data_store_type
        self.registered_topics = set()
        self.message_count = 0

        if data_store_config.data_store_mode == data_store_pb2.DataStoreMode.LOCAL_FILE:
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            path = os.path.join(data_store_config.store_path, f"dataset_{timestamp}")
            # rosbag2 creates the directory itself, so we don't strictly need makedirs, 
            # but good to ensure parent path exists.
            # However, rosbag2 will fail if the directory already exists and is not empty/valid bag.
            # We append timestamp so it should be unique.
            self.bag_path = path
            glog.info(f"DataStore initialized with LOCAL_FILE mode\nbag path: {self.bag_path}")
            
        elif data_store_config.data_store_mode == data_store_pb2.DataStoreMode.CLOUD_STORAGE:
            # TODO: Handle cloud storage sync
            self.bag_path = data_store_config.store_path
            glog.info(f"DataStore initialized with CLOUD_STORAGE mode\ncloud path: {self.bag_path}")
        else:
            glog.error(f"Invalid data store mode: {data_store_config.data_store_mode}")
            raise ValueError(f"Invalid data store mode: {data_store_config.data_store_mode}")

        # Initialize Rosbag2 Writer
        self.writer = rosbag2_py.SequentialWriter()
        storage_options = rosbag2_py.StorageOptions(uri=self.bag_path, storage_id='sqlite3')
        converter_options = rosbag2_py.ConverterOptions('', '')
        self.writer.open(storage_options, converter_options)

    def add_data(self, msg: Any, topic: str, timestamp: float = None):
        """Add a ROS2 message to the store in real-time.
        
        Args:
            msg: The ROS2 message object.
            topic: Topic name of the message.
            timestamp: Optional timestamp in seconds (uses current time if not provided).
        """
        if timestamp is None:
            timestamp = time.time()
        
        # Convert timestamp to nanoseconds (int) for rosbag2
        timestamp_ns = int(timestamp * 1e9)

        # Register topic if new
        if topic not in self.registered_topics:
            try:
                topic_type_name = get_ros2_type_name(msg)
            except ValueError as e:
                # Fallback or error
                glog.error(f"Failed to resolve type name for topic {topic}: {e}")
                # Attempt fallback?
                msg_type = type(msg)
                topic_type_name = msg_type.__module__.replace('.', '/') + '/' + msg_type.__name__

            topic_metadata = rosbag2_py.TopicMetadata(
                name=topic,
                type=topic_type_name,
                serialization_format='cdr')
            
            self.writer.create_topic(topic_metadata)
            self.registered_topics.add(topic)

        # Serialize and write
        serialized_msg = serialize_message(msg)
        self.writer.write(topic, serialized_msg, timestamp_ns)
        self.message_count += 1
        
        # Auto-save (For bag, we rely on built-in flush, but we can trigger post-process check here if needed)
        # For now, we only rely on explicit shutdown save for post-processing.

    def save_to_disk(self, path: str = None):
        """Post-process the recorded bag file into the target dataset format.
        
        Args:
            path: Output directory for the processed dataset. If None, uses bag_path parent.
        """
        glog.info(f"Starting save_to_disk. Message count: {self.message_count}")
        
        # Ensure writer is flushed/closed
        # There is no explicit close() in python api for SequentialWriter in older rosbag2 versions, 
        # but let's assume we just stop writing.
        
        # Force delete writer to ensure file handle is released
        if hasattr(self, 'writer') and self.writer:
            del self.writer
            self.writer = None
            # Force garbage collection to ensure __del__ is called on the writer
            import gc
            gc.collect()
        
        if not self.message_count:
             glog.warning("No data to save (message_count is 0).")
             return

        if path is None:
            path = self.bag_path + "_processed"

        os.makedirs(path, exist_ok=True)
        
        try:
            glog.info(f"Converting bag at {self.bag_path} to dataset...")
            # Convert bag to Dataset
            dataset = self._convert_bag_to_dataset(self.bag_path)
            glog.info(f"Conversion successful. Dataset size: {len(dataset)}")

            # Convert to target data type
            if self.data_store_type == data_store_pb2.DataType.JSONL:
                jsonl_path = os.path.join(path, "data.jsonl")
                dataset.to_json(jsonl_path)
                glog.info(f"Saved JSONL to {jsonl_path}")

            elif self.data_store_type == data_store_pb2.DataType.CSV:
                csv_path = os.path.join(path, "data.csv")
                dataset.to_csv(csv_path)
                glog.info(f"Converted to CSV at {csv_path}")
                
            elif self.data_store_type == data_store_pb2.DataType.PARQUET:
                parquet_path = os.path.join(path, "data.parquet")
                dataset.to_parquet(parquet_path)
                glog.info(f"Converted to Parquet at {parquet_path}")
                
            elif self.data_store_type == data_store_pb2.DataType.HUGGING_FACE_DATASET:
                dataset.save_to_disk(path)
                glog.info(f"Saved HF Dataset to {path}")
            
            glog.info(f"Post-processing complete. Data saved to {path}")
            
        except Exception as e:
            glog.error(f"Failed to post-process dataset: {e}")
            import traceback
            traceback.print_exc()

    def _convert_bag_to_dataset(self, bag_path: str) -> Dataset:
        """Read rosbag and convert to HuggingFace Dataset."""
        from rosidl_runtime_py.convert import message_to_ordereddict
        from rclpy.serialization import deserialize_message
        
        reader = rosbag2_py.SequentialReader()
        storage_options = rosbag2_py.StorageOptions(uri=bag_path, storage_id='sqlite3')
        converter_options = rosbag2_py.ConverterOptions('', '')
        reader.open(storage_options, converter_options)

        topics_and_types = reader.get_all_topics_and_types()
        type_map = {t.name: t.type for t in topics_and_types}

        data_list = []
        
        while reader.has_next():
            (topic, data, t) = reader.read_next()
            msg_type_name = type_map[topic]
            msg_class = get_message(msg_type_name)
            msg = deserialize_message(data, msg_class)
            
            msg_dict = message_to_ordereddict(msg)
            # Add metadata
            entry = {
                "topic": topic,
                "timestamp": t / 1e9, # Convert ns back to seconds
                **msg_dict
            }
            data_list.append(entry)

        # Convert list of dicts to dict of lists for efficient Dataset creation
        if not data_list:
            return Dataset.from_dict({})
            
        dataset_dict = {}
        # Collect all keys
        all_keys = set().union(*(d.keys() for d in data_list))
        
        for key in all_keys:
            dataset_dict[key] = [item.get(key) for item in data_list]
            
        return Dataset.from_dict(dataset_dict)

    def clear(self):
        """Clear all stored data (not really applicable for bag, but reset counter)."""
        self.message_count = 0
    
    def __len__(self) -> int:
        """Return the number of stored messages."""
        return self.message_count