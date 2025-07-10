import random
import time
import logging as glog
from ai.base_ai_layer import BaseAiLayer

# Configure logging
glog.basicConfig(level=glog.INFO, format='%(levelname)s: %(message)s')

# This will be available at runtime because of the bazel dependencies.
from robot.nexus.proto import nexus_packet_pb2

from ai.policy.factory import create_policy_config, create_policy

# Create a default policy instance.
# In a real scenario, the config might be loaded from a file.
dt_config = create_policy_config("decision_transformer")
policy = create_policy(dt_config)


def get_mock_action_from_decision_transformer(serialized_input_packet):
    """
    Deserializes a NexusModelInputPacket, creates a mock NexusModelOutputPacket,
    and returns it serialized.
    """
    try:
        input_packet = nexus_packet_pb2.NexusModelInputPacket()
        input_packet.ParseFromString(serialized_input_packet)
        glog.info(f"Input packet parsed successfully.")

        output_packet = nexus_packet_pb2.NexusModelOutputPacket()
        output_packet.timestamp = int(time.time() * 1e9)
        
        # TODO: Add get_action() here.

    except Exception as e:
        glog.error(f"Error in Python AI function: {e}")
        # Return empty packet on error
        return nexus_packet_pb2.NexusModelOutputPacket().SerializeToString() 
    

def generate_mock_ai_output(serialized_input_packet):
    """
    Deserializes a NexusModelInputPacket, creates a mock NexusModelOutputPacket,
    and returns it serialized.
    """
    
    try:
        input_packet = nexus_packet_pb2.NexusModelInputPacket()
        input_packet.ParseFromString(serialized_input_packet)
        glog.info(f"Input packet parsed successfully.")

        output_packet = nexus_packet_pb2.NexusModelOutputPacket()
        output_packet.timestamp = int(time.time() * 1e9)

        # Fake action packets for sts3215_driver.
        for i in range(1, 7):
            action_packet = output_packet.action_packets.add()
            action_packet.timestamp = int(time.time() * 1e9)
            action_packet.action_id = "sts3215_driver_" + str(i)
            action_packet.sts3215_action.position = random.randint(1950, 2050)

        result = output_packet.SerializeToString()
        glog.info(f"Serialization complete, result length: {len(result)}")
        return result
        
    except Exception as e:
        glog.error(f"Error in Python AI function: {e}")
        # Return empty packet on error
        return nexus_packet_pb2.NexusModelOutputPacket().SerializeToString() 