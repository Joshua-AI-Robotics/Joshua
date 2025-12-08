import os
import sys
import unittest

# How to run: `python ros2/python_bridge_backend_test.py` from repo root (no hardware required).
# Ensure repository root is on sys.path for local test execution.
TEST_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TEST_DIR, os.pardir))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from ros2 import python_bridge_backend as backend
from ros2.python_bridge_backend import ActuatorMapping, BridgeEnvelope, EncoderMapping, LoopbackBridgeBackend


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


class PybricksBridgeBackendTest(unittest.TestCase):
    def setUp(self):
        # Preserve globals that we will monkeypatch.
        self._orig_deserialize = backend.deserialize_message
        self._orig_serialize = backend.serialize_message
        self._orig_float32 = backend.Float32
        self._orig_hub_cls = backend.SpikeHubClient

    def tearDown(self):
        backend.deserialize_message = self._orig_deserialize
        backend.serialize_message = self._orig_serialize
        backend.Float32 = self._orig_float32
        backend.SpikeHubClient = self._orig_hub_cls

    def test_outbound_maps_to_hub_command(self):
        # Arrange fake serialization and hub.
        class FakeMsg:
            def __init__(self, data):
                self.data = data

        class FakeHub:
            def __init__(self):
                self.commands = []

            async def set_motor_target(self, port, value):
                self.commands.append((port, value))

        backend.deserialize_message = lambda payload, msg_type: FakeMsg(0.5)

        pb_backend = backend.PybricksBridgeBackend(logger=None)
        pb_backend._actuators = {
            "cmd": ActuatorMapping(
                topic="cmd",
                port="A",
                name="motor_a",
                encoder_data_mode=1,  # RAW passthrough
                limits=(0.0, 100.0),
            )
        }
        pb_backend._running = True

        envelope = BridgeEnvelope(topic="cmd", msg_type=FakeMsg, payload=b"ignored")
        fake_hub = FakeHub()

        # Act
        import asyncio

        asyncio.run(pb_backend._handle_outbound(envelope, fake_hub))

        # Assert
        self.assertEqual(fake_hub.commands, [("A", 0.5)])

    def test_encoder_poll_publishes_envelope(self):
        # Arrange fake Float32 and serialization.
        class FakeFloat32:
            def __init__(self):
                self.data = 0.0

        backend.Float32 = FakeFloat32
        backend.serialize_message = lambda msg: f"bytes:{msg.data}".encode("ascii")

        class FakeHub:
            def __init__(self, backend_ref):
                self._backend = backend_ref
                self.reads = 0

            async def read_encoder(self, port):
                self.reads += 1
                # Stop after first read to exit loop.
                self._backend._running = False
                return 42.0

        pb_backend = backend.PybricksBridgeBackend(logger=None)
        pb_backend._encoders = {
            "enc": EncoderMapping(topic="enc", port="B", name="encoder_b", publish_rate_hz=None)
        }
        pb_backend._running = True
        fake_hub = FakeHub(pb_backend)

        # Act
        import asyncio

        asyncio.run(pb_backend._poll_encoders(fake_hub))

        # Re-enable running flag to read from queue via poll_inbound.
        pb_backend._running = True
        inbound = pb_backend.poll_inbound()
        self.assertIsNotNone(inbound)
        self.assertEqual(inbound.topic, "enc")
        self.assertEqual(inbound.payload, b"bytes:42.0")


if __name__ == "__main__":
    unittest.main()
