import random
import sys
import threading

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
from std_msgs.msg import Float32

# Protobuf generated modules
from config.proto import config_pb2
from ros2 import node_runner as node_runner_py


class DataSubscriber(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)

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

    def subscribe_callback(self, msg):
        self.get_logger().info('Received: "%f"' % msg.data)



def main(argv=None):
    return node_runner_py.run_node(DataSubscriber, logger_name="data_subscriber", argv=argv)


if __name__ == "__main__":
    sys.exit(main())
