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

# Configure logging
glog.basicConfig(level=glog.INFO, format='%(levelname)s: %(message)s')

# This will be available at runtime because of the bazel dependencies.
from robot.nexus.proto import nexus_packet_pb2

from ai.policy.factory import create_policy_config, create_policy


# Global cache for policies to avoid re-instantiation on every call.
_policy_cache = {}


def _get_or_create_policy(policy_name: str):
    """
    Lazily initializes and caches a policy object.
    If the policy is already in the cache, it returns the cached instance.
    Otherwise, it creates a new one, caches it, and returns it.
    """
    if policy_name not in _policy_cache:
        glog.info(f"Policy '{policy_name}' not found in cache. Creating a new instance.")
        config = create_policy_config(policy_name)
        _policy_cache[policy_name] = create_policy(config)
    return _policy_cache[policy_name]


def get_mock_action_from_decision_transformer(serialized_input_packet):
    try:
        policy = _get_or_create_policy("decision_transformer")
        
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