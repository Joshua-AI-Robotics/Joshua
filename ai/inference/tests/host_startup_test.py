import os
import unittest
from unittest.mock import call, create_autospec, patch

from ai.proto.ai_model_pb2 import RANDOM_NOISE

from ai.inference.adapter import InferenceAdapter
from ai.inference.types import AdapterSpec
from config.proto.config_pb2 import Config
from ros2.proto.ros2_data_type_pb2 import FLOAT32
from ros2.utils.ros_python_paths import setup_import_paths

setup_import_paths()

import rclpy  # noqa: E402

from ai.inference.host import InferenceHost  # noqa: E402


class HostStartupTest(unittest.TestCase):
    def setUp(self):
        os.environ["ROS_LOG_DIR"] = os.environ["TEST_TMPDIR"]
        rclpy.init(args=[])
        self.addCleanup(rclpy.shutdown)
        self.config = Config()
        model = self.config.ai.models.single_models.add(model_type=RANDOM_NOISE)
        model.node.id = 1
        model.node.subscriptions.add(topic="input", ros2_data_type=FLOAT32)
        model.node.publishers.add(topic="output", ros2_data_type=FLOAT32)
        self.adapter = create_autospec(InferenceAdapter, instance=True)
        self.adapter.spec.return_value = AdapterSpec()
        factory = patch(
            "ai.inference.host.registry.create_adapter", return_value=self.adapter
        )
        factory.start()
        self.addCleanup(factory.stop)

    def test_rejected_named_policy_closes_adapter_before_node_cleanup(self):
        self.adapter.spec.return_value.motor_policy.version = 1
        destroy_node = InferenceHost.destroy_node

        def destroy_after_close(host):
            self.adapter.close.assert_called_once_with()
            return destroy_node(host)

        with patch.object(
            InferenceHost,
            "destroy_node",
            autospec=True,
            side_effect=destroy_after_close,
        ) as destroy, patch.object(InferenceHost, "_setup_publishers") as publishers:
            with self.assertRaisesRegex(ValueError, "cannot dispatch named"):
                InferenceHost("policy_startup_test", 1, self.config)
        self.assertEqual(
            self.adapter.method_calls,
            [call.validate(), call.initialize(), call.spec(), call.close()],
        )
        publishers.assert_not_called()
        destroy.assert_called_once()

    def test_partial_initialization_preserves_startup_error_if_close_fails(self):
        self.adapter.initialize.side_effect = RuntimeError("initialization failed")
        self.adapter.close.side_effect = ValueError("close failed")
        with self.assertRaisesRegex(RuntimeError, "initialization failed"):
            InferenceHost("policy_startup_test", 1, self.config)
        self.adapter.spec.assert_not_called()
        self.adapter.close.assert_called_once_with()

    def test_scalar_adapter_stays_open_until_shutdown(self):
        host = InferenceHost("policy_startup_test", 1, self.config)
        self.addCleanup(host.destroy_node)
        self.adapter.close.assert_not_called()
        host.shutdown()
        self.adapter.close.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
