import unittest
from collections import defaultdict
from unittest.mock import Mock, patch

from std_msgs.msg import Int32

from ai.train.data_store import DataStore


class DataStoreTimeTest(unittest.TestCase):
    def setUp(self):
        # Exercise the recording methods without opening a bag or dataset files.
        self.store = DataStore.__new__(DataStore)
        self.store.writer = Mock()
        self.store.is_recording = True
        self.store.total_message_count = 0
        self.store.topic_message_counts = defaultdict(int)

    def test_bag_receive_time_uses_exact_robot_nanoseconds(self):
        stamp = 1_700_000_000_123_456_789
        with patch("ai.train.data_store.robot_time_ns", return_value=stamp):
            self.store.add_data(Int32(data=42), "sample")
        self.assertEqual(self.store.writer.write.call_args.args[2], stamp)
        self.assertEqual(self.store.topic_message_counts["sample"], 1)
        with patch("ai.train.data_store.robot_time_ns") as now:
            self.store.add_data(Int32(), "sample", timestamp=0.0)
            now.assert_not_called()
        self.assertEqual(self.store.writer.write.call_args.args[2], 0)

    def test_episode_marker_uses_the_same_robot_clock(self):
        self.store.is_recording = False
        self.store._get_next_episode_index = Mock(return_value=7)
        stamp = 1_700_000_000_123_456_789
        with patch("ai.train.data_store.robot_time_ns", return_value=stamp):
            self.assertEqual(self.store.start_recording(), 7)
        self.assertEqual(
            self.store.writer.write.call_args.args[0], "/dataset/episode_index"
        )
        self.assertEqual(self.store.writer.write.call_args.args[2], stamp)


if __name__ == "__main__":
    unittest.main()
