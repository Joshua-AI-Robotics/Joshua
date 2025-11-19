"""Real-time data store for ROS2 messages in HuggingFace format."""

import os
import time
from typing import Any, Dict, List
from datasets import Dataset
from datetime import datetime

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
        self.save_path = data_store_config.store_path
    
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
        """Save the dataset to disk in HuggingFace format.
        
        Args:
            path: Directory path where to save the dataset.
        """
        if not self.data:
            print("No data to save.")
            return
        
        # Create directory if it doesn't exist
        os.makedirs(path, exist_ok=True)
        
        # Convert to dataset and save
        dataset = self.to_dataset()
        dataset.save_to_disk(path)
        
        # Store save path for auto-save
        self.save_path = path
        
        print(f"Saved {len(self.data)} entries to {path}")
    
    def clear(self):
        """Clear all stored data."""
        self.data = []
    
    def __len__(self) -> int:
        """Return the number of stored messages."""
        return len(self.data)