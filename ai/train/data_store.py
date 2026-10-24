"""Real-time data store for ROS2 messages and post-processing to various formats."""

import json
import os
import time
from datetime import datetime
from typing import Any, Dict, List

import glog
import rclpy
import rosbag2_py
from datasets import Dataset, Features, Sequence, Value
from rclpy.serialization import serialize_message
from rosidl_runtime_py.utilities import get_message
from std_msgs.msg import Int32

from ai.proto import data_store_pb2
from ros2.ros2_type_resolver import get_ros2_type_name, get_ros2_type_string_from_enum


class DataStore:
    """Real-time data store for ROS2 messages using rosbag2 and post-processing to various formats."""

    def __init__(self, data_store_config: data_store_pb2.DataStore):
        """Initialize the data store.

        Args:
            data_store_config: data_store.proto.
        """
        self.data_store_type = data_store_config.data_store_type
        self.registered_topics = set()
        self.total_message_count = 0
        self.topic_message_counts = {}
        self.is_recording = False
        self.current_episode_index = -1
        # Root path for storing persistent data like episode index
        self.store_root = data_store_config.store_path

        # Setup data store configuration.
        if data_store_config.data_store_mode == data_store_pb2.DataStoreMode.LOCAL_FILE:
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            path = os.path.join(data_store_config.store_path, f"dataset_{timestamp}")
            # rosbag2 creates the directory itself, so we don't strictly need makedirs,
            # but good to ensure parent path exists.
            # However, rosbag2 will fail if the directory already exists and is not empty/valid bag.
            # We append timestamp so it should be unique.
            self.bag_path = path
            glog.info(
                f"DataStore initialized with LOCAL_FILE mode\nbag path: {self.bag_path}"
            )

        elif (
            data_store_config.data_store_mode
            == data_store_pb2.DataStoreMode.CLOUD_STORAGE
        ):
            # TODO: Handle cloud storage sync
            self.bag_path = data_store_config.store_path
            glog.info(
                f"DataStore initialized with CLOUD_STORAGE mode\ncloud path: {self.bag_path}"
            )
        else:
            glog.error(f"Invalid data store mode: {data_store_config.data_store_mode}")
            raise ValueError(
                f"Invalid data store mode: {data_store_config.data_store_mode}"
            )

        # Initialize Rosbag2 Writer
        self.writer = rosbag2_py.SequentialWriter()
        storage_options = rosbag2_py.StorageOptions(
            uri=self.bag_path, storage_id="sqlite3"
        )
        converter_options = rosbag2_py.ConverterOptions("", "")
        self.writer.open(storage_options, converter_options)

        # Register topics.
        for subscription in data_store_config.node.subscriptions:
            topic = subscription.topic
            topic_type_name = get_ros2_type_string_from_enum(
                subscription.ros2_data_type
            )
            topic_metadata = rosbag2_py.TopicMetadata(
                name=topic, type=topic_type_name, serialization_format="cdr"
            )
            self.writer.create_topic(topic_metadata)
            self.registered_topics.add(topic)
            self.topic_message_counts[topic] = 0

        # Register episode index topic
        episode_topic_metadata = rosbag2_py.TopicMetadata(
            name="/dataset/episode_index",
            type="std_msgs/msg/Int32",
            serialization_format="cdr",
        )
        self.writer.create_topic(episode_topic_metadata)

    def _get_next_episode_index(self) -> int:
        """Get the next episode index from the persistent counter file."""
        index_file = os.path.join(self.store_root, ".last_episode_index")
        current_index = 0
        if os.path.exists(index_file):
            try:
                with open(index_file, "r") as f:
                    current_index = int(f.read().strip()) + 1
            except Exception as e:
                glog.warning(f"Failed to read episode index file: {e}")

        # Save new index immediately to reserve it
        try:
            with open(index_file, "w") as f:
                f.write(str(current_index))
        except Exception as e:
            glog.warning(f"Failed to write episode index file: {e}")

        return current_index

    def start_recording(self):
        """Start recording a new episode."""
        if self.is_recording:
            glog.warning("Already recording.")
            return

        self.current_episode_index = self._get_next_episode_index()
        self.is_recording = True
        glog.info(f"Started recording episode {self.current_episode_index}")

        # Write episode index to bag
        msg = Int32()
        msg.data = self.current_episode_index
        timestamp_ns = int(time.time() * 1e9)
        self.writer.write(
            "/dataset/episode_index", serialize_message(msg), timestamp_ns
        )
        return self.current_episode_index

    def stop_recording(self):
        """Stop recording the current episode."""
        if not self.is_recording:
            glog.warning("Not recording.")
            return

        self.is_recording = False
        glog.info(f"Stopped recording episode {self.current_episode_index}")

    def add_data(self, msg: Any, topic: str, timestamp: float = None):
        """Add a ROS2 message to the store in real-time.

        Args:
            msg: The ROS2 message object.
            topic: Topic name of the message.
            timestamp: Optional timestamp in seconds (uses current time if not provided).
        """
        if not self.is_recording:
            return

        if timestamp is None:
            timestamp = time.time()

        # Convert timestamp to nanoseconds (int) for rosbag2
        timestamp_ns = int(timestamp * 1e9)

        # Serialize and write
        serialized_msg = serialize_message(msg)
        self.writer.write(topic, serialized_msg, timestamp_ns)
        self.total_message_count += 1
        self.topic_message_counts[topic] += 1
        # Post-processing on shutdown().

    def post_process(self, path: str = None):
        """Post-process the recorded bag file into the target dataset format.

        Args:
            path: Output directory for the processed dataset. If None, uses bag_path parent.
        """
        glog.info(
            f"Starting Post-Processing. Total message count: {self.total_message_count}"
        )
        glog.info(f"Topic message counts: {self.topic_message_counts}")

        # Ensure writer is flushed/closed
        # There is no explicit close() in python api for SequentialWriter in older rosbag2 versions,
        # but let's assume we just stop writing.

        # Force delete writer to ensure file handle is released
        if hasattr(self, "writer") and self.writer:
            del self.writer
            self.writer = None
            # Force garbage collection to ensure __del__ is called on the writer
            import gc

            gc.collect()

        if not self.total_message_count:
            glog.warning("No data to save (total_message_count is 0).")
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
        """Generic converter for ANY ROS 2 bag to HuggingFace Dataset."""
        from datasets import Dataset

        def gen():
            import rosbag2_py
            from rclpy.serialization import deserialize_message
            from rosidl_runtime_py.utilities import get_message

            from ros2.ros2_type_resolver import build_entry_for_message

            # Setup Reader
            reader = rosbag2_py.SequentialReader()
            storage_options = rosbag2_py.StorageOptions(
                uri=bag_path, storage_id="sqlite3"
            )
            converter_options = rosbag2_py.ConverterOptions("", "")
            reader.open(storage_options, converter_options)

            # Cache type maps
            topic_types = reader.get_all_topics_and_types()
            type_map = {t.name: t.type for t in topic_types}

            current_episode_index = -1

            # Pre-scan the bag to discover all possible keys across all topics to ensure a robust schema.
            # We read the first message of each topic to determine the union of all fields.

            discovered_keys = {"topic", "timestamp", "episode_index"}

            # Helper to inspect keys from a message without fully processing/yielding
            def get_keys_for_msg(m_type, m):
                dummy_base = {"topic": "dummy", "timestamp": 0.0}
                row = build_entry_for_message(dummy_base, m_type, m)
                return set(row.keys())

            # Pass 1: Scan types for all registered topics.
            # Re-open reader since SequentialReader doesn't support seek(0).

            scan_reader = rosbag2_py.SequentialReader()
            scan_reader.open(storage_options, converter_options)

            seen_topics = set()
            all_topics_in_bag = set(type_map.keys())

            while scan_reader.has_next():
                if len(seen_topics) == len(all_topics_in_bag):
                    break

                (topic, data, t) = scan_reader.read_next()
                if topic in seen_topics or topic not in type_map:
                    continue

                msg_type = type_map[topic]
                msg_class = get_message(msg_type)
                msg = deserialize_message(data, msg_class)

                keys = get_keys_for_msg(msg_type, msg)
                discovered_keys.update(keys)
                seen_topics.add(topic)

            del scan_reader

            # Let's define a safe wrapper that yields consistent rows based on discovered keys
            def safe_build_entry(base, m_type, m):
                row = build_entry_for_message(base, m_type, m)
                for key in discovered_keys:
                    if key not in row:
                        row[key] = None
                return row

            while reader.has_next():
                (topic, data, t) = reader.read_next()

                # Handle episode index updates
                if topic == "/dataset/episode_index":
                    msg_type = "std_msgs/msg/Int32"
                    msg_class = get_message(msg_type)
                    msg = deserialize_message(data, msg_class)
                    current_episode_index = msg.data
                    continue

                if topic not in type_map:
                    continue

                msg_type = type_map[topic]
                msg_class = get_message(msg_type)
                msg = deserialize_message(data, msg_class)

                base_entry = {
                    "topic": topic,
                    "timestamp": t / 1e9,
                    "episode_index": current_episode_index,
                }

                yield safe_build_entry(base_entry, msg_type, msg)

        try:
            return Dataset.from_generator(gen)
        except Exception:
            return Dataset.from_generator(gen, keep_in_memory=True)

    def clear(self):
        """Clear all stored data (not really applicable for bag, but reset counter)."""
        self.total_message_count = 0

    def get_total_message_count(self) -> int:
        """Return the total number of messages stored."""
        return self.total_message_count

    def get_topic_message_count(self, topic: str) -> int:
        """Return the number of messages stored for each topic."""
        return self.topic_message_counts.get(topic, 0)
