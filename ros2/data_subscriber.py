import sys
import os
import time

from rclpy.node import Node
from std_msgs.msg import Float32

# Protobuf generated modules
from config.proto import config_pb2
from ros2 import node_runner as node_runner_py

# Import DataStore
from ai.train.data_store import DataStore


class DataSubscriber(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)

        # Initialize DataStore with auto-save every 10 messages
        self.data_store = DataStore(auto_save_interval=10)
        
        # Set save path from config or use default
        save_dir = os.path.expanduser("/tmp/Joshua/data/sample_recordings")
        os.makedirs(save_dir, exist_ok=True)
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        self.save_path = os.path.join(save_dir, f"dataset_{timestamp}")
        self.data_store.save_path = self.save_path
        
        self.get_logger().info(f'Data will be saved to: {self.save_path}')

        # Subscribers
        self.subscribers = []
        self.topics = ['sample_topic']

        for topic in self.topics:        
            self.subscribers.append(self.create_subscription(
                Float32,
                topic,
                self.subscribe_callback,
                10))
            self.get_logger().info(f'Subscribing topic: {topic}')
        
        self.message_count = 0

    def subscribe_callback(self, msg):
        """Callback to handle incoming ROS2 messages and store them."""
        self.message_count += 1
        
        # Convert ROS2 message to dictionary
        msg_data = {
            "topic": "sample_topic",
            "value": float(msg.data),
            "message_count": self.message_count
        }
        
        # Add to data store (real-time storage)
        self.data_store.add_data(msg_data)
        
        self.get_logger().info(
            f'Received: {msg.data:.4f} | Total stored: {len(self.data_store)}'
        )
    
    def shutdown(self):
        """Save data on shutdown."""
        if len(self.data_store) > 0:
            self.get_logger().info('Saving final dataset...')
            self.data_store.save_to_disk(self.save_path)
            self.get_logger().info(f'Saved {len(self.data_store)} messages to {self.save_path}')



def main(argv=None):
    return node_runner_py.run_node(DataSubscriber, logger_name="data_subscriber", argv=argv)


if __name__ == "__main__":
    sys.exit(main())
