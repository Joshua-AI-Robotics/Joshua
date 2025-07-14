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

    def store_as_lerobot_dataset(self, serialized_input_packet, serialized_output_packet, episode_index=0):
        """
        Stores input and output packets as LeRobot dataset format for supervised learning.
        
        Args:
            serialized_input_packet: Serialized NexusModelInputPacket
            serialized_output_packet: Serialized NexusModelOutputPacket (required for supervised learning)
            episode_index: Current episode index for tracking
        """
        try:            
            # Parse input packet
            input_packet = nexus_packet_pb2.NexusModelInputPacket()
            input_packet.ParseFromString(serialized_input_packet)
            
            # Parse output packet (required for supervised learning)
            output_packet = nexus_packet_pb2.NexusModelOutputPacket()
            output_packet.ParseFromString(serialized_output_packet)
            
            # Store the data
            self.dataset_storage.add_data_point(input_packet, output_packet, episode_index)
            
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
    Handles storage and conversion of robot data to LeRobot dataset format.
    """
    
    def __init__(self):
        self.dataset = {
            "observation": {
                "image": [],
                "joint_pos": [],
            },
            "action": [],
            "episode_index": [],
            "timestamp": [],
            "episode_length": []
        }
        self.current_episode_data = []
        self.episode_lengths = {}
        
    def add_data_point(self, input_packet, output_packet=None, episode_index=0):
        """
        Adds a data point to the dataset.
        
        Args:
            input_packet: NexusModelInputPacket containing sensor data
            output_packet: Optional NexusModelOutputPacket containing actions
            episode_index: Current episode index
        """
        # Extract observations from input packet
        observations = self._extract_observations(input_packet)
        
        # Extract actions from output_packet
        actions = self._extract_actions(output_packet) if output_packet else None
        
        # Store the data point
        data_point = {
            "observation": observations,
            "action": actions,
            "episode_index": episode_index,
            "timestamp": input_packet.timestamp
        }
        
        self.current_episode_data.append(data_point)
        
        # Update episode length
        if episode_index not in self.episode_lengths:
            self.episode_lengths[episode_index] = 0
        self.episode_lengths[episode_index] += 1
        
    def _extract_observations(self, input_packet):
        """
        Extracts observations from NexusModelInputPacket.
        
        Args:
            input_packet: NexusModelInputPacket
            
        Returns:
            Dict containing image and joint position data
        """
        observations = {
            "image": None,
            "joint_pos": []
        }
        
        # Extract encoder positions (joint positions)
        encoder_positions = []
        for perception_packet in input_packet.perception_packets:
            if perception_packet.perception_id.startswith("sts3215_encoder"):
                if perception_packet.HasField("encoder_perception"):
                    encoder_positions.append(perception_packet.encoder_perception.position)
        
        observations["joint_pos"] = encoder_positions
        
        # Extract camera image
        for perception_packet in input_packet.perception_packets:
            if perception_packet.perception_id.startswith("cv_camera"):
                if perception_packet.HasField("camera_perception"):
                    # Convert bytes to PIL Image
                    image_data = perception_packet.camera_perception.image_data
                    try:
                        image = Image.open(io.BytesIO(image_data))
                        observations["image"] = image
                    except Exception as e:
                        glog.warning(f"Failed to decode image: {e}")
                        observations["image"] = None
        
        return observations
    
    def _extract_actions(self, output_packet):
        """
        Extracts actions from NexusModelOutputPacket.
        
        Args:
            output_packet: NexusModelOutputPacket
            
        Returns:
            List of motor positions
        """
        if not output_packet:
            return None
            
        actions = []
        for action_packet in output_packet.action_packets:
            if action_packet.action_id.startswith("sts3215_driver"):
                if action_packet.HasField("sts3215_action"):
                    actions.append(action_packet.sts3215_action.position)
        
        return actions
    
    def save_dataset(self, output_dir: str):
        """
        Saves the dataset to disk in LeRobot format.
        
        Args:
            output_dir: Directory to save the dataset
        """
        output_path = Path(output_dir)
        output_path.mkdir(parents=True, exist_ok=True)
        
        # Convert current episode data to LeRobot format
        lerobot_dataset = self._convert_to_lerobot_format()
        
        # Save as JSON (LeRobot compatible format)
        dataset_file = output_path / "dataset.json"
        with open(dataset_file, 'w') as f:
            json.dump(lerobot_dataset, f, indent=2)
        
        # Save images separately if they exist
        if any(dp["observation"]["image"] is not None for dp in self.current_episode_data):
            images_dir = output_path / "images"
            images_dir.mkdir(exist_ok=True)
            
            for i, data_point in enumerate(self.current_episode_data):
                if data_point["observation"]["image"]:
                    image_path = images_dir / f"image_{i:06d}.jpg"
                    data_point["observation"]["image"].save(image_path, "JPEG")
                    # Update the image reference to file path
                    data_point["observation"]["image"] = str(image_path)
        
        # Save metadata
        metadata = {
            "num_episodes": len(self.episode_lengths),
            "episode_lengths": self.episode_lengths,
            "total_data_points": len(self.current_episode_data),
            "sensor_types": ["camera", "encoder"],
            "action_types": ["sts3215_motor"]
        }
        
        metadata_file = output_path / "metadata.json"
        with open(metadata_file, 'w') as f:
            json.dump(metadata, f, indent=2)
        
    
    def _convert_to_lerobot_format(self):
        """
        Converts the collected data to LeRobot format.
        
        Returns:
            Dict in LeRobot dataset format
        """
        # Initialize LeRobot format
        lerobot_data = {
            "observation": {
                "image": [],
                "joint_pos": [],
            },
            "action": [],
            "episode_index": [],
            "timestamp": [],
            "episode_length": []
        }
        
        # Convert data points
        for i, data_point in enumerate(self.current_episode_data):
            # Add observations
            if data_point["observation"]["image"]:
                # Use image file path instead of PIL Image object
                image_path = f"images/image_{i:06d}.jpg"
                lerobot_data["observation"]["image"].append(image_path)
            else:
                lerobot_data["observation"]["image"].append(None)
                
            lerobot_data["observation"]["joint_pos"].append(data_point["observation"]["joint_pos"])
            
            # Add actions
            lerobot_data["action"].append(data_point["action"])
            
            # Add metadata
            lerobot_data["episode_index"].append(data_point["episode_index"])
            lerobot_data["timestamp"].append(data_point["timestamp"])
        
        # Add episode lengths
        for episode_idx in sorted(self.episode_lengths.keys()):
            lerobot_data["episode_length"].append(self.episode_lengths[episode_idx])
        
        return lerobot_data 