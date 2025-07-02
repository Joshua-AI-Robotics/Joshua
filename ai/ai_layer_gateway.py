import random
import time
import sys
import os
import logging as glog

# Configure logging
glog.basicConfig(level=glog.INFO, format='%(levelname)s: %(message)s')

# Add protobuf paths to ensure google module is found
# TODO: Update the project setting to avoid this manual dependency check.
if hasattr(sys, '_getframe'):
    # Running under Bazel - add runfiles protobuf paths
    current_dir = os.path.dirname(os.path.abspath(__file__))
    runfiles_root = None
    path_parts = current_dir.split(os.sep)
    for i, part in enumerate(path_parts):
        if part.endswith('.runfiles'):
            runfiles_root = os.sep.join(path_parts[:i+1])
            break
    
    if runfiles_root:
        protobuf_paths = [
            os.path.join(runfiles_root, 'protobuf+', 'python'),
            os.path.join(runfiles_root, 'rules_python++pip+ubirobotics_pip_deps_310_protobuf', 'site-packages')
        ]
        for path in protobuf_paths:
            if os.path.exists(path) and path not in sys.path:
                sys.path.insert(0, path)

# This will be available at runtime because of the bazel dependencies.
from robot.nexus.proto import nexus_packet_pb2

def generate_mock_ai_output(serialized_input_packet):
    """
    Deserializes a NexusModelInputPacket, creates a mock NexusModelOutputPacket,
    and returns it serialized.
    """
    
    try:
        input_packet = nexus_packet_pb2.NexusModelInputPacket()
        input_packet.ParseFromString(serialized_input_packet)
        glog.info(f"Input packet parsed successfully, ID: {input_packet.model_input_id}")

        output_packet = nexus_packet_pb2.NexusModelOutputPacket()
        output_packet.timestamp = int(time.time() * 1e9)
        output_packet.model_output_id = "py_model_output_0"

        # Fake action packets for sts3215_driver.
        for i in range(1, 6):
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