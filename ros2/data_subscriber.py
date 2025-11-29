import functools
import os
import sys
import time

from rclpy.node import Node

from ai.train.data_store import DataStore
from config.proto import config_pb2
from ros2 import node_runner as node_runner_py
from ros2.proto import node_pb2
from ros2.ros2_type_resolver import resolve_message_class_from_enum
from ros2.utils.qos_setting import create_qos_setting

DATA_STORE_DEBUG_LOG_INTERVAL = 10


class DataSubscriber(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)

        self.data_store = None
        self.subscription_list = []
        self.topic_message_counts = {}

        # Currently, only one data store is supported since one node can subscribe all topics.
        if len(config.ai.data_stores.single_data_stores) > 1:
            raise ValueError("Only one data store is supported")

        if len(config.ai.data_stores.single_data_stores) == 0:
            raise ValueError("No data stores found in config")

        self.data_store = DataStore(
            data_store_config=config.ai.data_stores.single_data_stores[0]
        )
        self._setup_subscriptions(config.ai.data_stores.single_data_stores[0].node)

    def _setup_subscriptions(self, node: node_pb2.Node) -> None:
        """
        Set up ROS2 subscriptions for data storage.
        """
        qos_setting = create_qos_setting(node.qos_setting)
        for subscription in node.subscriptions:
            message_type = resolve_message_class_from_enum(subscription.ros2_data_type)
            self.topic_message_counts[subscription.topic] = 0
            subscription = self.create_subscription(
                message_type,
                subscription.topic,
                functools.partial(self.subscribe_callback, topic=subscription.topic),
                qos_setting,
            )
            self.subscription_list.append(subscription)

    def subscribe_callback(self, msg, topic):
        """Callback to handle incoming ROS2 messages and store them."""
        self.topic_message_counts[topic] += 1
        self.data_store.add_data(msg, topic=topic)

        if self.topic_message_counts[topic] % DATA_STORE_DEBUG_LOG_INTERVAL == 0:
            self.get_logger().info(
                f"{topic} stored messages: {self.topic_message_counts[topic]}"
            )

    def shutdown(self):
        """Save data on shutdown."""
        # Use print since current scope is out of ros2 context.
        print(
            f"Shutdown initiated. DataStore items: {self.data_store.get_total_message_count()}"
        )
        if self.data_store.get_total_message_count() > 0:
            print("Saving final dataset...")
            sys.stdout.flush()
            # Fallback to bag_path parent + _processed if save_path doesn't exist
            save_path = getattr(
                self.data_store, "save_path", self.data_store.bag_path + "_processed"
            )
            self.data_store.post_process(save_path)
            print(
                f"Saved {self.data_store.get_total_message_count()} messages to {save_path}"
            )
            sys.stdout.flush()


def main(argv=None):
    return node_runner_py.run_node(
        DataSubscriber, logger_name="data_subscriber", argv=argv
    )


# How to test:
# On terminal, run:
# bazel run node_generator:joshua_main -- --config=config/config_preset/so100_leader_arm_encoder_publish.pbtxt
# On separate terminal, run:
# bazel run ros2:data_subscriber -- test 1 config/config_preset/sample_data_store.pbtxt
if __name__ == "__main__":
    sys.exit(main())
