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
        except Exception as e:
            glog.error(f"Failed to load {self.config.ai.policy_name} policy: {e}")
            raise e

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
            glog.info(f"{self.config.ai.policy_name} policy: Input packet parsed successfully.")

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

    def generate_mock_ai_output(self, serialized_input_packet):
        """
        Deserializes an input packet and returns a mock output packet without
        using a policy.
        """
        try:
            input_packet = nexus_packet_pb2.NexusModelInputPacket()
            input_packet.ParseFromString(serialized_input_packet)
            glog.info(f"Mock: Input packet parsed successfully.")

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
            return nexus_packet_pb2.NexusModelOutputPacket().SerializeToString() 