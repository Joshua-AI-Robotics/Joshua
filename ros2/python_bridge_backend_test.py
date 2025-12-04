import unittest

from ros2.python_bridge_backend import BridgeEnvelope, LoopbackBridgeBackend


class DummyMsg:
    pass


class LoopbackBridgeBackendTest(unittest.TestCase):
    def test_echoes_when_publisher_metadata_available(self):
        backend = LoopbackBridgeBackend()
        backend.start(metadata={"publishers": {"pybricks/events": DummyMsg}})

        envelope = BridgeEnvelope(topic="pybricks/commands", msg_type=DummyMsg, payload=b"ping")
        backend.push_outbound(envelope)
        mirrored = backend.poll_inbound()

        self.assertIsNotNone(mirrored)
        self.assertEqual(mirrored.topic, "pybricks/events")
        self.assertEqual(mirrored.msg_type, DummyMsg)
        self.assertEqual(mirrored.payload, b"ping")

        backend.stop()

    def test_drops_when_no_publishers(self):
        backend = LoopbackBridgeBackend()
        backend.start(metadata={"publishers": {}})

        envelope = BridgeEnvelope(topic="pybricks/commands", msg_type=DummyMsg, payload=b"ping")
        backend.push_outbound(envelope)

        self.assertIsNone(backend.poll_inbound())
        backend.stop()


if __name__ == "__main__":
    unittest.main()

