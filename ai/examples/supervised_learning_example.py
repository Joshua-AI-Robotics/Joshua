#!/usr/bin/env python3
"""
Example script demonstrating supervised learning dataset collection
using the updated LeRobot dataset storage functionality.
"""

import sys
import os
import time
import logging

# Add the parent directory to the path to import the gateway
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ai_layer_gateway import AILayerGateway
from robot.nexus.proto import nexus_packet_pb2
from config.proto import config_pb2

def create_mock_config():
    """Create a mock config for testing."""
    config = config_pb2.Config()
    config.ai.policy_name = "supervised_learning_policy"
    return config.SerializeToString()

def create_mock_input_packet():
    """Create a mock input packet with sensor data."""
    input_packet = nexus_packet_pb2.NexusModelInputPacket()
    input_packet.timestamp = int(time.time() * 1e9)
    
    # Add encoder perceptions (joint positions)
    for i in range(1, 7):
        perception = input_packet.perception_packets.add()
        perception.timestamp = int(time.time() * 1e9)
        perception.perception_id = f"sts3215_encoder_{i}"
        perception.encoder_perception.position = 1950 + i * 10  # Mock positions
    
    # Add camera perception (mock image data)
    camera_perception = input_packet.perception_packets.add()
    camera_perception.timestamp = int(time.time() * 1e9)
    camera_perception.perception_id = "cv_camera_0"
    # Create a simple mock image (1x1 pixel JPEG)
    mock_image_data = b'\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xdb\x00C\x00\x02\x01\x01\x01\x01\x01\x02\x01\x01\x01\x02\x02\x02\x02\x02\x04\x03\x02\x02\x02\x02\x05\x04\x04\x03\x04\x06\x05\x06\x06\x06\x05\x06\x06\x06\x07\t\x08\x06\x07\t\x07\x06\x06\x08\x0b\x08\t\n\n\n\n\n\x06\x08\x0b\x0c\x0b\n\x0c\t\n\n\n\n\xff\xdb\x00C\x01\x02\x02\x02\x02\x02\x02\x05\x03\x03\x05\n\x07\x06\x07\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\xff\xc0\x00\x11\x08\x00\x01\x00\x01\x03\x01"\x00\x02\x11\x01\x03\x11\x01\xff\xc4\x00\x1f\x00\x00\x01\x05\x01\x01\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\xff\xc4\x00\xb5\x10\x00\x02\x01\x03\x03\x02\x04\x03\x05\x05\x04\x04\x00\x00\x01}\x01\x02\x03\x00\x04\x11\x05\x12!1A\x06\x13Qa\x07"q\x142\x81\x91\xa1\x08#B\xb1\xc1\x15R\xd1\xf0$3br\x82\t\n\x16\x17\x18\x19\x1a%&\'()*456789:CDEFGHIJSTUVWXYZcdefghijstuvwxyz\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93\x94\x95\x96\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xff\xc4\x00\x1f\x01\x00\x03\x01\x01\x01\x01\x01\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\xff\xc4\x00\xb5\x11\x00\x02\x01\x02\x04\x04\x03\x04\x07\x05\x04\x04\x00\x01\x02w\x00\x01\x02\x03\x11\x04\x05!1\x06\x12AQ\x07aq\x13"2\x81\x08\x14B\x91\xa1\xb1\xc1\t#3R\xf0\x15br\xd1\n\x16$4\xe1%\xf1\x17\x18\x19\x1a&\'()*56789:CDEFGHIJSTUVWXYZcdefghijstuvwxyz\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93\x94\x95\x96\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xff\xda\x00\x0c\x03\x01\x00\x02\x11\x03\x11\x00?\x00\xe0\xe6\xff\x00\x82}\xfe\xcf\xf7y\xf3\xfe\x1a\xe9\xe4\x1e\xc02\x07\xe8}\xbf\xcfJ\xa1y\xff\x00\x04\xcf\xfd\x99\xef\xb2\xf2|/\xb0m\xd9\xe5K\x8e\xf5F\x1f\xdcW\xd4\x91i\xf0\xa1;\xfd1\xc7\xff\x00^'
    camera_perception.camera_perception.image_data = mock_image_data
    
    return input_packet.SerializeToString()

def create_mock_output_packet():
    """Create a mock output packet with meaningful actions (for supervised learning)."""
    output_packet = nexus_packet_pb2.NexusModelOutputPacket()
    output_packet.timestamp = int(time.time() * 1e9)
    
    # Add motor actions (these would be from a trained policy or human demonstration)
    for i in range(1, 7):
        action_packet = output_packet.action_packets.add()
        action_packet.timestamp = int(time.time() * 1e9)
        action_packet.action_id = f"sts3215_driver_{i}"
        # Simulate meaningful actions (not random)
        action_packet.sts3215_action.position = 2000 + i * 5  # Structured actions
    
    return output_packet.SerializeToString()

def main():
    """Main function demonstrating supervised learning dataset collection."""
    logging.basicConfig(level=logging.INFO)
    logger = logging.getLogger(__name__)
    
    logger.info("Initializing AILayerGateway for supervised learning...")
    
    # Create mock config and initialize gateway
    mock_config = create_mock_config()
    gateway = AILayerGateway(mock_config)
    
    logger.info("Creating supervised learning data packets...")
    
    # Simulate collecting supervised learning data for multiple episodes
    for episode in range(3):
        logger.info(f"Collecting supervised learning data for episode {episode}")
        
        # Simulate multiple timesteps per episode
        for timestep in range(5):
            # Create mock input and output packets
            input_packet = create_mock_input_packet()
            output_packet = create_mock_output_packet()
            
            # Store the data for supervised learning (BOTH input and output required)
            gateway.store_as_lerobot_dataset(
                input_packet, 
                output_packet,  # Required for supervised learning
                episode_index=episode
            )
            
            # Simulate time passing
            time.sleep(0.1)
    
    logger.info("Saving supervised learning dataset...")
    
    # Save the dataset
    output_dir = "supervised_learning_dataset"
    gateway.save_dataset(output_dir)
    
    logger.info(f"Supervised learning dataset saved to {output_dir}")
    logger.info("This dataset can be used for:")
    logger.info("  - Behavioral cloning")
    logger.info("  - Imitation learning") 
    logger.info("  - Supervised policy training")
    logger.info("  - Action prediction models")

if __name__ == "__main__":
    main() 