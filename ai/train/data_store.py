"""Real-time data store for ROS2 messages in HuggingFace format."""

import os
import time
import json
from typing import Any, Dict, List
from datasets import Dataset
from datetime import datetime
import glog

from ai.proto import data_store_pb2

class DataStore:
    """Real-time data store for ROS2 messages with HuggingFace dataset format."""
    
    def __init__(self, data_store_config: data_store_pb2.DataStore):
        """Initialize the data store.
        
        Args:
            data_store_config: data_store.proto.
        """
        self.data = []
        self.auto_save_interval = data_store_config.auto_save_interval
        self.data_store_type = data_store_config.data_store_type

        if data_store_config.data_store_mode == data_store_pb2.DataStoreMode.LOCAL_FILE:
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            path = os.path.join(data_store_config.store_path, f"dataset_{timestamp}")
            os.makedirs(path, exist_ok=True)
            self.save_path = path
            glog.info(f"DataStore initialized with LOCAL_FILE mode\nlocal path: {self.save_path}")
            
        elif data_store_config.data_store_mode == data_store_pb2.DataStoreMode.CLOUD_STORAGE:
            self.save_path = data_store_config.store_path
            glog.info(f"DataStore initialized with CLOUD_STORAGE mode\ncloud path: {self.save_path}")
        else:
            glog.error(f"Invalid data store mode: {data_store_config.data_store_mode}")
            raise ValueError(f"Invalid data store mode: {data_store_config.data_store_mode}")
    
    def add_data(self, msg_data: Dict[str, Any], timestamp: float = None):
        """Add a ROS2 message to the store in real-time.
        
        Args:
            msg_data: Dictionary containing the message data.
            timestamp: Optional timestamp (uses current time if not provided).
        """
        if timestamp is None:
            timestamp = time.time()
        
        # Add timestamp to the data
        data_entry = {"timestamp": timestamp, **msg_data}
        self.data.append(data_entry)
        
        # Auto-save if interval reached
        if self.auto_save_interval > 0 and len(self.data) % self.auto_save_interval == 0:
            if self.save_path:
                self.save_to_disk(self.save_path)
    
    def to_dataset(self) -> Dataset:
        """Convert stored data to HuggingFace Dataset format.
        
        Returns:
            A HuggingFace Dataset object.
        """
        if not self.data:
            raise ValueError("No data available. Add data first using add_data()")
        
        # Convert list of dicts to dict of lists (HuggingFace format)
        dataset_dict = {}
        for key in self.data[0].keys():
            dataset_dict[key] = [item.get(key) for item in self.data]
        
        return Dataset.from_dict(dataset_dict)
    
    def save_to_disk(self, path: str):
        """Save the dataset to disk in configured format.

        Always saves as JSONL first for performance, then converts to target format.
        
        Args:
            path: Directory path where to save the dataset.
        """
        if not self.data:
            glog.warning("No data to save.")
            return
        
        # Create directory if it doesn't exist
        os.makedirs(path, exist_ok=True)
        
        # Always save as JSONL first (fastest, bypasses HF conversion)
        jsonl_path = os.path.join(path, "data.jsonl")
        with open(jsonl_path, 'w') as f:
            for entry in self.data:
                f.write(json.dumps(entry) + '\n')
        glog.info(f"Saved JSONL to {jsonl_path}")
        
        # If target is JSONL, we are done
        if self.data_store_type == data_store_pb2.DataType.JSONL:
            self.save_path = path
            glog.info(f"Saved {len(self.data)} entries to {path}")
            return

        # Convert to dataset for other formats
        dataset = self.to_dataset()

        # Convert to target data type if different
        if self.data_store_type == data_store_pb2.DataType.CSV:
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
        
        # Store save path for auto-save
        self.save_path = path
        
        glog.info(f"Saved {len(self.data)} entries to {path}")
    
    def clear(self):
        """Clear all stored data."""
        self.data = []
    
    def __len__(self) -> int:
        """Return the number of stored messages."""
        return len(self.data)