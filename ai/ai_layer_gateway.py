import sys
import os

# --- BEGIN ROBUST PATH DISCOVERY ---
# This block correctly locates the Bazel runfiles directory and adds all
# necessary `site-packages` to the Python path. This is required because
# the standard environment variables are not available when Python is
# embedded via pybind11.
try:
    # Get the directory of the current script.
    current_script_path = os.path.dirname(os.path.abspath(__file__))
    
    # Walk upwards to find the root of the .runfiles directory.
    runfiles_root = current_script_path
    while not runfiles_root.endswith(".runfiles"):
        runfiles_root = os.path.dirname(runfiles_root)
        if runfiles_root == "/":
            raise RuntimeError("Could not find .runfiles directory.")

    # Walk downwards from the root to find all site-packages and add them.
    for root, dirs, files in os.walk(runfiles_root):
        if "site-packages" in dirs:
            site_packages_path = os.path.join(root, "site-packages")
            if site_packages_path not in sys.path:
                sys.path.insert(0, site_packages_path)
except Exception as e:
    print(f"Error during Python path setup: {e}", file=sys.stderr)
# --- END ROBUST PATH DISCOVERY ---

import random
import time
import logging as glog
import json
import pickle
from pathlib import Path
from typing import Dict, List, Any, Optional
import numpy as np
from PIL import Image
import io
import pandas as pd
from collections import defaultdict

# Configure logging
glog.basicConfig(level=glog.INFO, format='%(levelname)s: %(message)s')

# This will be available at runtime because of the bazel dependencies.
from robot.nexus.proto import nexus_packet_pb2
from config.proto import config_pb2


class AILayerGateway:
    """
    Manages AI policies, providing a clean interface for C++ to call.
    An instance of this class should be created by the C++ AIExecutor,
    which will then call its methods for inference.
    """
    def __init__(self, serialized_config):
        glog.info("Initializing AILayerGateway and loading policies.")
        try:
            # Deserialize the Config from C++.
            self.config = config_pb2.Config()
            self.config.ParseFromString(serialized_config)

            # Load policies once during initialization.
            # TODO: Load policies here.
            # self.policy = xx
            glog.info(f"{self.config.ai.policy_name} policy loaded successfully.")
            
            # Initialize dataset storage
            self.dataset_storage = LeRobotDatasetStorage()
            
        except Exception as e:
            glog.error(f"Failed to load {self.config.ai.policy_name} policy: {e}")
            raise e

    def store_as_lerobot_dataset(self, serialized_input_packet, serialized_output_packet, episode_index=0, is_last_step=False):
        """
        Stores input and output packets as LeRobot dataset format for supervised learning.
        
        Args:
            serialized_input_packet: Serialized NexusModelInputPacket (REQUIRED)
            serialized_output_packet: Serialized NexusModelOutputPacket (REQUIRED for supervised learning)
            episode_index: Current episode index for tracking
            is_last_step: Flag indicating if this is the last step in an episode
        """
        try:
            # Parse input packet
            input_packet = nexus_packet_pb2.NexusModelInputPacket()
            input_packet.ParseFromString(serialized_input_packet)
            
            # Parse output packet (required for supervised learning)
            output_packet = nexus_packet_pb2.NexusModelOutputPacket()
            output_packet.ParseFromString(serialized_output_packet)
            
            # Store the data
            self.dataset_storage.add_data_point(input_packet, output_packet, episode_index, is_last_step)
            
        except Exception as e:
            glog.error(f"Python: Error storing supervised learning data as LeRobot dataset: {e}")
            import traceback
            glog.error(f"Python: Traceback: {traceback.format_exc()}")

    def save_dataset(self, output_dir: str = "robot_dataset"):
        """
        Saves the collected dataset to disk in LeRobot format.
        
        Args:
            output_dir: Directory to save the dataset
        """
        try:
            self.dataset_storage.save_dataset(output_dir)
            glog.info(f"Dataset saved to {output_dir}")
        except Exception as e:
            glog.error(f"Error saving dataset: {e}")

    def get_action(self, serialized_input_packet):
        """
        Runs inference using the pre-loaded policy.
        """
        # if not self.policy:
        #     glog.error(f"{self.config.ai.policy_name} policy not available.")
        #     return nexus_packet_pb2.NexusModelOutputPacket().SerializeToString()
        
        try:
            input_packet = nexus_packet_pb2.NexusModelInputPacket()
            input_packet.ParseFromString(serialized_input_packet)

            output_packet = nexus_packet_pb2.NexusModelOutputPacket()
            output_packet.timestamp = int(time.time() * 1e9)
            
            # TODO: Replace this mock implementation with a call to the actual
            # policy like: action = policy.get_action(input_packet)
            # and then populate the output_packet from that action.

            for i in range(1, 7):
                action_packet = output_packet.action_packets.add()
                action_packet.timestamp = int(time.time() * 1e9)
                action_packet.action_id = "sts3215_driver_" + str(i)
                action_packet.sts3215_action.position = random.randint(1950, 2050)

            return output_packet.SerializeToString()

        except Exception as e:
            glog.error(f"Error in get_action: {e}")
            return nexus_packet_pb2.NexusModelOutputPacket().SerializeToString()

    def get_fake_action(self, serialized_input_packet):
        """
        Deserializes an input packet and returns a mock output packet without
        using a policy.
        """
        try:
            input_packet = nexus_packet_pb2.NexusModelInputPacket()
            input_packet.ParseFromString(serialized_input_packet)

            output_packet = nexus_packet_pb2.NexusModelOutputPacket()
            output_packet.timestamp = int(time.time() * 1e9)

            # Fake action packets for sts3215_driver.
            for i in range(1, 7):
                action_packet = output_packet.action_packets.add()
                action_packet.timestamp = int(time.time() * 1e9)
                action_packet.action_id = "sts3215_driver_" + str(i)
                action_packet.sts3215_action.position = random.randint(1950, 2050)

            result = output_packet.SerializeToString()
            return result
            
        except Exception as e:
            glog.error(f"Error in Python AI function: {e}")
            return nexus_packet_pb2.NexusModelOutputPacket().SerializeToString()


class LeRobotDatasetStorage:
    """
    Handles storage and conversion of robot data to a LeRobot-compliant
    Hugging Face dataset format.
    """
    
    def __init__(self):
        self.steps = []
        self.global_index = 0
        self.frame_indices = defaultdict(int)
        
    def add_data_point(self, input_packet, output_packet, episode_index, is_last_step):
        """
        Adds a single data point (a step) to the buffer.
        """
        state, image_bytes = self._extract_observations(input_packet)
        action = self._extract_actions(output_packet)

        step = {
            'index': self.global_index,
            'episode_index': episode_index,
            'frame_index': self.frame_indices[episode_index],
            'timestamp': input_packet.timestamp / 1e9,  # Convert ns to seconds
            'observation.state': state,
            'action': action,
            'next': not is_last_step,
            'done': is_last_step,
            # Temporary fields, will be replaced/removed before saving.
            'raw_image': image_bytes, 
            'image_path': None,
        }

        self.steps.append(step)
        
        self.global_index += 1
        self.frame_indices[episode_index] += 1
        
    def _extract_observations(self, input_packet):
        """
        Extracts observations into a flat state vector and raw image bytes.
        """
        # Extract encoder positions for the state vector.
        # Ensure a consistent order by sorting based on the perception_id suffix.
        encoder_perceptions = [
            p for p in input_packet.perception_packets 
            if p.perception_id.startswith("sts3215_encoder")
        ]
        encoder_perceptions.sort(key=lambda p: int(p.perception_id.split('_')[-1]))
        state = [p.encoder_perception.position for p in encoder_perceptions]
        
        # Extract camera image bytes.
        image_bytes = None
        for p in input_packet.perception_packets:
            if p.perception_id.startswith("cv_camera"):
                image_bytes = p.camera_perception.image_data
                break # Assume one camera
        
        return state, image_bytes
    
    def _extract_actions(self, output_packet):
        """
        Extracts actions into a flat vector.
        """
        # Ensure a consistent order by sorting based on the action_id suffix.
        action_packets = sorted(
            output_packet.action_packets, 
            key=lambda a: int(a.action_id.split('_')[-1])
        )
        action = [a.sts3215_action.position for a in action_packets]
        return action
    
    def save_dataset(self, output_dir: str):
        """
        Saves the buffered data to a LeRobot-compliant dataset structure
        with Parquet files.
        """
        if not self.steps:
            glog.warning("No data to save.")
            return

        output_path = Path(output_dir)
        images_path = output_path / "images"
        chunk_path = output_path / "chunk-0"
        
        images_path.mkdir(parents=True, exist_ok=True)
        chunk_path.mkdir(exist_ok=True)

        # Group steps by episode
        episodes = defaultdict(list)
        for step in self.steps:
            episodes[step['episode_index']].append(step)

        # Process and save each episode
        for episode_idx, steps in episodes.items():
            for step in steps:
                # Save image and create relative path
                if step['raw_image']:
                    ep_images_path = images_path / f"episode_{episode_idx:05d}"
                    ep_images_path.mkdir(exist_ok=True)
                    
                    frame_idx = step['frame_index']
                    image_path = ep_images_path / f"frame_{frame_idx:05d}.jpg"
                    
                    step['image_path'] = str(image_path.relative_to(output_path))
                    with open(image_path, 'wb') as f:
                        f.write(step['raw_image'])
                
                del step['raw_image'] # Remove temporary raw image data

            # Create and save Parquet file for the episode
            df = pd.DataFrame(steps)
            # Reorder columns for clarity
            cols = ['index', 'frame_index', 'episode_index', 'timestamp', 'observation.state', 'image_path', 'action', 'next', 'done']
            df = df[cols]
            
            parquet_path = chunk_path / f"episode_{episode_idx:05d}.parquet"
            df.to_parquet(parquet_path)

        # Save metadata and stats
        self._save_metadata(output_path)
        glog.info(f"Dataset saved successfully to {output_dir}")

    def _save_metadata(self, output_path: Path):
        # Create dataset_info.json
        dataset_info = {
            "key_to_features": {
                "observation.state": {"shape": [len(self.steps[0]['observation.state'])], "dtype": "float32"},
                "image_path": {"shape": [], "dtype": "string"},
                "action": {"shape": [len(self.steps[0]['action'])], "dtype": "float32"},
                "episode_index": {"shape": [], "dtype": "int64"},
                "frame_index": {"shape": [], "dtype": "int64"},
                "index": {"shape": [], "dtype": "int64"},
                "timestamp": {"shape": [], "dtype": "float64"},
                "next": {"shape": [], "dtype": "bool"},
                "done": {"shape": [], "dtype": "bool"},
            }
        }
        with open(output_path / "dataset_info.json", 'w') as f:
            json.dump(dataset_info, f, indent=2)

        # Create stats.json
        all_states = np.array([s['observation.state'] for s in self.steps])
        all_actions = np.array([s['action'] for s in self.steps])
        
        stats = {
            "observation.state": {
                "mean": all_states.mean(axis=0).tolist(),
                "std": all_states.std(axis=0).tolist(),
            },
            "action": {
                "mean": all_actions.mean(axis=0).tolist(),
                "std": all_actions.std(axis=0).tolist(),
            }
        }
        with open(output_path / "stats.json", 'w') as f:
            json.dump(stats, f, indent=2) 