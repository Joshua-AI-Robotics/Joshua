import functools
import os
import sys

from rclpy.node import Node
from std_msgs.msg import Bool

from ai.train.data_store import DataStore
from config.proto import config_pb2
from ros2 import node_runner as node_runner_py
from ros2.proto import node_pb2
from ros2.ros2_type_resolver import resolve_message_class_from_enum
from ros2.utils.qos_setting import create_qos_setting


class DataSubscriber(Node):
    def __init__(
        self, node_name: str, node_id: int, config: config_pb2.Config
    ):
        super().__init__(node_name)

        self.data_store = None
        self.subscription_list = []
        self.topic_message_counts = {}

        # One data store per node; one node can subscribe to all topics.
        if len(config.ai.data_stores.single_data_stores) > 1:
            raise ValueError("Only one data store is supported")

        if len(config.ai.data_stores.single_data_stores) == 0:
            raise ValueError("No data stores found in config")

        store_config = config.ai.data_stores.single_data_stores[0]
        self.data_store = DataStore(data_store_config=store_config)
        self._setup_subscriptions(store_config.node)

        # Subscribe to recording control topic
        self.create_subscription(
            Bool, "recording_control", self.recording_control_callback, 10
        )

        self.next_episode_index = 0
        index_file = os.path.join(
            self.data_store.store_root, ".last_episode_index"
        )
        if os.path.exists(index_file):
            try:
                with open(index_file, "r") as f:
                    self.next_episode_index = int(f.read().strip()) + 1
            except (ValueError, OSError):
                pass

        print(
            "[IDLE] Waiting for data... "
            f"(Next Episode Index: {self.next_episode_index}) | "
            "Send True to 'recording_control' to start, False to stop."
        )

    def _setup_subscriptions(self, node: node_pb2.Node) -> None:
        """
        Set up ROS2 subscriptions for data storage.
        """
        qos_setting = create_qos_setting(node.qos_setting)
        for subscription in node.subscriptions:
            message_type = resolve_message_class_from_enum(
                subscription.ros2_data_type
            )
            self.topic_message_counts[subscription.topic] = 0
            subscription = self.create_subscription(
                message_type,
                subscription.topic,
                functools.partial(
                    self.subscribe_callback, topic=subscription.topic
                ),
                qos_setting,
            )
            self.subscription_list.append(subscription)

    def recording_control_callback(self, msg):
        """Handle recording control signals."""
        if msg.data:
            used_index = self.data_store.start_recording()
            if used_index is not None:
                self.next_episode_index = used_index + 1
        else:
            self.data_store.stop_recording()

    def subscribe_callback(self, msg, topic):
        """Callback to handle incoming ROS2 messages and store them."""
        self.topic_message_counts[topic] += 1
        self.data_store.add_data(msg, topic=topic)

        state = (
            "RECORDING"
            if getattr(self.data_store, "is_recording", False)
            else "DATA AVAILABLE"
        )

        # Build status string
        if state == "RECORDING":
            counts_str = " | ".join(
                [f"{k}: {v}" for k, v in self.topic_message_counts.items()]
            )
            display_str = f"[{state}] {counts_str}"
        else:
            display_str = (
                f"[{state}] Send True to 'recording_control' to start, "
                "False to stop. "
                f"(Next Episode Index: {self.next_episode_index})"
            )

        # Update terminal line
        sys.stdout.write(f"\r{display_str}\033[K")
        sys.stdout.flush()

    def shutdown(self):
        """Save data on shutdown."""
        # Use print since current scope is out of ros2 context.
        total = self.data_store.get_total_message_count()
        print(f"Shutdown initiated. DataStore items: {total}")
        if total > 0:
            print("Saving final dataset...")
            sys.stdout.flush()
            # Fallback to bag_path parent + _processed if save_path missing
            save_path = getattr(
                self.data_store,
                "save_path",
                self.data_store.bag_path + "_processed",
            )
            self.data_store.post_process(save_path)
            print(f"Saved {total} messages to {save_path}")
            sys.stdout.flush()


def main(argv=None):
    return node_runner_py.run_node(
        DataSubscriber, logger_name="data_subscriber", argv=argv
    )


# How to test:
# In separate Docker terminals, run:
# make run-u22 CONFIG=config/config_preset/example/sample_data_publish.pbtxt
# make run-u22 CONFIG=config/config_preset/example/sample_data_store.pbtxt
if __name__ == "__main__":
    sys.exit(main())
