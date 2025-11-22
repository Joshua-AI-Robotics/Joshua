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

DATA_STORE_DEBUG_LOG_INTERVAL = 10

class DataSubscriber(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)

        self.data_store = DataStore(data_store_config=config.ai.data_store)
        self.subscribers = []
        self.topic_message_counts = {}

        for data_info in config.ai.data_store.data_infos:
            message_type = resolve_message_class_from_enum(data_info.ros2_data_type)
            topic = data_info.topic
            self.topic_message_counts[topic] = 0
            self.subscribers.append(self.create_subscription(
                message_type,
                topic,
                functools.partial(self.subscribe_callback, topic=topic),
                10))
            self.get_logger().info(f'Subscribing to topic: {topic} (type: {message_type.__name__})')
            

    def subscribe_callback(self, msg, topic):
        """Callback to handle incoming ROS2 messages and store them."""
        self.topic_message_counts[topic] += 1
        self.data_store.add_data(msg, topic=topic)
        
        if self.topic_message_counts[topic] % DATA_STORE_DEBUG_LOG_INTERVAL == 0:
            self.get_logger().info(f'{topic} stored messages: {self.topic_message_counts[topic]}')
    
    def shutdown(self):
        """Save data on shutdown."""
        # Use print since current scope is out of ros2 context.
        print(f'Shutdown initiated. DataStore items: {len(self.data_store)}')
        if len(self.data_store) > 0:
            print('Saving final dataset...')
            sys.stdout.flush()
            # Fallback to bag_path parent + _processed if save_path doesn't exist
            save_path = getattr(self.data_store, 'save_path', self.data_store.bag_path + "_processed")
            self.data_store.save_to_disk(save_path)
            print(f'Saved {len(self.data_store)} messages to {save_path}')
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
