import unittest
from unittest import mock

from robot.action.factory import action_factory
from robot.action.proto import action_pb2
from robot.comm.proto import comm_pb2


class ActionFactoryTest(unittest.TestCase):
    def _make_single_action(self, actuator_type: int):
        single = action_pb2.SingleAction()
        single.action_type = action_pb2.ActionType.ACTUATOR
        actuator = single.actuator
        actuator.actuator_type = actuator_type
        actuator.actuator_name = "test"
        actuator.id = 1
        actuator.comm.comm_type = comm_pb2.BLE
        actuator.spike_motor_config.port = "A"
        return single

    def test_spike_motor_driver(self):
        single = self._make_single_action(action_pb2.ActuatorType.SPIKE_MOTOR)
        with mock.patch.object(
            action_factory, "PybricksMotorDriver", autospec=True
        ) as patched:
            instance = patched.return_value
            driver = action_factory.create_action(single)
            patched.assert_called_once()
            instance.init.assert_called_once()
            self.assertEqual(driver, instance)

    def test_mock_motor_type_now_rejected(self):
        # MOCK_MOTOR moved to the C++ board layer (docs/BOARD_LAYER_RFC.md
        # §10 Phase 9); the Python factory no longer handles it.
        single = self._make_single_action(action_pb2.ActuatorType.MOCK_MOTOR)
        with self.assertRaises(ValueError):
            action_factory.create_action(single)


if __name__ == "__main__":
    unittest.main()
