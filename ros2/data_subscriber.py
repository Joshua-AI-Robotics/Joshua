import sys
import os
import time

from rclpy.node import Node
from std_msgs.msg import Float32

from config.proto import config_pb2
from ros2 import node_runner as node_runner_py
from ai.train.data_store import DataStore

class DataSubscriber(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)

        # Initialize DataStore with auto-save every 10 messages
        self.data_store = DataStore(data_store_config=config.ai.data_store)

        # Subscribers
        # TODO: Parse with proto config.
        self.subscribers = []
        self.topics = ['sample_topic']

        for topic in self.topics:        
            self.subscribers.append(self.create_subscription(
                Float32,
                topic,
                self.subscribe_callback,
                10))
            self.get_logger().info(f'Subscribing to topics: {self.topics}')
        
        self.message_count = 0

    def subscribe_callback(self, msg):
        """Callback to handle incoming ROS2 messages and store them."""
        self.message_count += 1
        
        # TODO: Parse with proto config.
        # Convert ROS2 message to dictionary
        msg_data = {
            "topic": "sample_topic",
            "value": float(msg.data),
            "message_count": self.message_count
        }
        
        # Add to data store (real-time storage)
        self.data_store.add_data(msg_data)
        
        self.get_logger().info(f'Received: {msg.data:.4f} | Total stored: {len(self.data_store)}')
    
    def shutdown(self):
        """Save data on shutdown."""
        if len(self.data_store) > 0:
            self.get_logger().info('Saving final dataset...')
            self.data_store.save_to_disk(self.data_store.save_path)
            self.get_logger().info(f'Saved {len(self.data_store)} messages to {self.data_store.save_path}')



def main(argv=None):
    return node_runner_py.run_node(DataSubscriber, logger_name="data_subscriber", argv=argv)

# How to test:
# On terminal, run:
# ros2 topic pub -r 1 /sample_topic std_msgs/msg/Float32 "{data: 3.14}"
# On separate terminal, run:
# bazel run ros2:data_subscriber -- test 1 config/config_preset/sample_data_store.pbtxt
if __name__ == "__main__":
    sys.exit(main())
