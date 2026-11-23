import os
import subprocess
import unittest
from dataclasses import replace
from pathlib import Path

from google.protobuf import text_format
from robot.action.proto.action_packet_pb2 import JointCommand

from ai.inference.motor_policy import (
    ChoiceAnswer,
    EvidenceSample,
    MeasuredPosition,
    PolicyRequest,
    RequestId,
    Rgb8,
    SampleId,
    evaluate_choice,
)
from ai.inference.proto.motor_policy_pb2 import MotorPolicy
from ai.inference.types import ActionCommand, ChannelRole, Observation
from ai.models.random_noise.adapter import RandomNoiseAdapter
from config.proto.config_pb2 import Config


class MotorPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"]
        fixture = root / "ai/inference/testdata/rotary_gripper.pbtxt"
        subprocess.run(
            [root / "config/joshua_validate_config", "--config", fixture], check=True
        )
        cls.policy = text_format.Parse(fixture.read_text(), Config()).ai.motor_policy

    def setUp(self):
        self.measured = MeasuredPosition(
            "gripper_hinge", SampleId("encoder", 3, 8), 0.5, MotorPolicy.Joint.RADIANS
        )
        self.request = PolicyRequest(
            1,
            RequestId("run1", "configA", 2, 12),
            "grasp",
            (
                EvidenceSample(
                    "workpiece", SampleId("camera", 0, 2), Rgb8(1, 1, b"rgb")
                ),
                EvidenceSample(
                    "instruction", SampleId("operator", 0, 5), "Grasp the part"
                ),
                EvidenceSample(
                    "inspection", SampleId("detector", 1, 9), '{"present": true}'
                ),
            ),
            self.measured,
        )

    def replay(self, label, request=None):
        return evaluate_choice(
            self.policy,
            request or self.request,
            lambda q, req: ChoiceAnswer(req.id, q.id, label),
        )

    def test_non_head_replay(self):
        sink = []
        for i, label in enumerate(("close", "hold", "open")):
            request = replace(self.request, id=replace(self.request.id, sequence=i))
            result = self.replay(label, request)
            self.assertEqual(result.request_id, request.id)
            self.assertEqual(
                result.samples,
                tuple(e.id for e in request.evidence) + (self.measured.id,),
            )
            if isinstance(result.outcome, JointCommand):
                sink.append(result.outcome)
            else:
                self.assertEqual(result.outcome, "no_command")
        self.assertEqual(
            sink,
            [
                JointCommand(
                    joint_name="gripper_hinge",
                    position=value,
                    position_encoding=JointCommand.POSITION_SI,
                )
                for value in (0.375, 1.0)
            ],
        )

    def test_absolute_question_uses_only_its_declared_inputs(self):
        request = replace(
            self.request,
            question_id="park",
            evidence=(self.request.evidence[1],),
            measured=None,
        )
        result = self.replay("park", request)
        self.assertEqual(result.samples, (request.evidence[0].id,))
        self.assertEqual(
            result.outcome,
            JointCommand(
                joint_name="gripper_hinge",
                position=0.0,
                position_encoding=JointCommand.POSITION_SI,
            ),
        )

    def test_legacy_adapter_keeps_scalar_output(self):
        config = Config()
        model = config.ai.models.single_models.add()
        model.node.publishers.add()
        model.node.subscriptions.add()
        model.random_noise_config.output_size = 1
        model.random_noise_config.output_ranges.add(low=0.0, high=1.0)
        adapter = RandomNoiseAdapter(model, config)
        adapter.validate()
        self.assertFalse(adapter.spec().HasField("motor_policy"))
        sample = Observation(0, "input", ChannelRole.SCALAR, 0.5, 0)
        for _ in range(adapter.BUFFER_TRIGGER - 1):
            self.assertIsNone(adapter.on_observation(sample))
        (command,) = adapter.on_observation(sample)
        self.assertIsInstance(command, ActionCommand)
        self.assertEqual(command.publisher_index, 0)
        self.assertFalse(command.normalized)
        self.assertTrue(0 <= command.value <= 1)

    def test_rejects_invalid_evidence_before_calling_provider(self):
        original = self.request.evidence
        for index, payload in (
            (0, Rgb8(1, 1, b"r")),
            (0, Rgb8(0, 1, b"")),
            (0, "rgb"),
            (1, " "),
            (2, "{"),
            (2, '{"bad": NaN}'),
            (2, "[1e999]"),
        ):
            evidence = list(original)
            evidence[index] = replace(evidence[index], payload=payload)
            with self.subTest(index=index, payload=payload):
                self.assert_rejected(
                    replace(self.request, evidence=tuple(evidence)), "invalid_evidence"
                )

    def assert_rejected(self, request, reason):
        def unexpected(*args):
            self.fail("invalid input reached the provider")

        self.assertEqual(
            evaluate_choice(self.policy, request, unexpected).outcome, reason
        )

    def test_input_binding(self):
        evidence = self.request.evidence
        for inputs in (
            evidence[1:],
            (evidence[0], evidence[0], evidence[2]),
            (*evidence, evidence[0]),
        ):
            self.assert_rejected(
                replace(self.request, evidence=inputs), "invalid_inputs"
            )
        self.assert_rejected(
            replace(self.request, question_id="other"), "unknown_question"
        )
        self.assert_rejected(replace(self.request, version=2), "incompatible_version")
        self.assert_rejected(
            replace(self.request, id=replace(self.request.id, run="")),
            "invalid_request_identity",
        )
        invalid = replace(evidence[0], id=SampleId("", 0, 0))
        self.assert_rejected(
            replace(self.request, evidence=(invalid, *evidence[1:])),
            "invalid_sample_identity",
        )

    def test_answer_must_match_request_question_and_choices(self):
        for answer in (
            ChoiceAnswer(replace(self.request.id, generation=1), "grasp", "close"),
            ChoiceAnswer(self.request.id, "unknown", "close"),
            None,
        ):
            with self.subTest(answer=answer):
                result = evaluate_choice(self.policy, self.request, lambda q, r: answer)
                self.assertEqual(result.outcome, "mismatched_answer")
        self.assertEqual(self.replay("unexpected").outcome, "unknown_choice")

    def test_position_mapping(self):
        self.assert_rejected(
            replace(self.request, measured=None), "missing_measured_position"
        )
        for measured in (
            replace(self.measured, joint_name="other"),
            replace(self.measured, unit=0),
            replace(self.measured, value=float("nan")),
            replace(self.measured, value=1.1),
        ):
            self.assert_rejected(
                replace(self.request, measured=measured), "invalid_measured_position"
            )
        for measured, expected in ((0.125, 0.0), (0.0, "target_out_of_bounds")):
            request = replace(
                self.request, measured=replace(self.measured, value=measured)
            )
            outcome = self.replay("close", request).outcome
            if isinstance(outcome, JointCommand):
                self.assertTrue(outcome.HasField("position"))
                outcome = outcome.position
            self.assertEqual(outcome, expected)


if __name__ == "__main__":
    unittest.main()
