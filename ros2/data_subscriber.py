import sys
import os
import time
import functools

from rclpy.node import Node
from std_msgs.msg import Float32
from rosidl_runtime_py.convert import message_to_ordereddict

from config.proto import config_pb2
from ros2 import node_runner as node_runner_py
from ros2.ros2_type_resolver import resolve_message_class_from_enum
from ai.train.data_store import DataStore

class DataSubscriber(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)

        # Initialize DataStore
        self.data_store = DataStore(data_store_config=config.ai.data_store)

        # Subscribers
        self.subscribers = []
        
        for data_info in config.ai.data_store.data_infos:
            message_type = resolve_message_class_from_enum(data_info.ros2_data_type)
            topic = data_info.topic
            
            self.subscribers.append(self.create_subscription(
                message_type,
                topic,
                functools.partial(self.subscribe_callback, topic=topic),
                10))
            self.get_logger().info(f'Subscribing to topic: {topic} (type: {message_type.__name__})')
        
        self.message_count = 0

    def subscribe_callback(self, msg, topic):
        """Callback to handle incoming ROS2 messages and store them."""
        self.message_count += 1
        
        # Add to data store (real-time storage)
        # We pass the raw message object now, DataStore handles serialization/bagging
        self.data_store.add_data(msg, topic=topic)
        
        if self.message_count % 10 == 0:
            self.get_logger().info(f'Total stored messages: {len(self.data_store)}')
    
    def shutdown(self):
        """Save data on shutdown."""
        print(f'Shutdown initiated. DataStore items: {len(self.data_store)}')
        if len(self.data_store) > 0:
            # Use print since current scope is out of ros2 context.
            print('Saving final dataset...')
            sys.stdout.flush()
            self.data_store.save_to_disk(self.data_store.save_path)
            print(f'Saved {len(self.data_store)} messages to {self.data_store.save_path}')
            sys.stdout.flush()



def main(argv=None):
    return node_runner_py.run_node(DataSubscriber, logger_name="data_subscriber", argv=argv)

# How to test:
# On terminal, run:
# ros2 topic pub -r 1 /sample_topic std_msgs/msg/Float32 "{data: 3.14}"
# On separate terminal, run:
# bazel run ros2:data_subscriber -- test 1 config/config_preset/sample_data_store.pbtxt
if __name__ == "__main__":
    sys.exit(main())
