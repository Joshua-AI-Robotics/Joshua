import random
import time
import sys
import os

# Add protobuf paths to ensure google module is found
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
    print("DEBUG: Python AI function called", flush=True)
    
    try:
        print("DEBUG: Parsing input packet...", flush=True)
        input_packet = nexus_packet_pb2.NexusModelInputPacket()
        input_packet.ParseFromString(serialized_input_packet)
        print(f"DEBUG: Input packet parsed successfully, ID: {input_packet.model_input_id}", flush=True)

        print("DEBUG: Creating output packet...", flush=True)
        output_packet = nexus_packet_pb2.NexusModelOutputPacket()
        output_packet.timestamp = int(time.time() * 1e9)
        output_packet.model_output_id = "py_model_output_0"

        print("DEBUG: Adding action packets...", flush=True)
        # Mimic the logic from the C++ version
        for i in range(1, 6):
            action_packet = output_packet.action_packets.add()
            action_packet.timestamp = int(time.time() * 1e9)
            action_packet.action_id = str(i)
            action_packet.sts3215_action.position = random.randint(1950, 2050)

        print("DEBUG: Serializing output packet...", flush=True)
        result = output_packet.SerializeToString()
        print(f"DEBUG: Serialization complete, result length: {len(result)}", flush=True)
        return result
        
    except Exception as e:
        print(f"DEBUG: Error in Python AI function: {e}", flush=True)
        # Return empty packet on error
        return nexus_packet_pb2.NexusModelOutputPacket().SerializeToString() 