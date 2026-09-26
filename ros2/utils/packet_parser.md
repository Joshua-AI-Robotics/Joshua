# Packet parser

`packet_parser.cc` bridges compiled ROS message types and internal packets.
`packet_parser.py` supplies matching field access for Python consumers.

`ActionPacket.joint` is the motion payload for every actuator. Optional position,
velocity, and effort distinguish omission from zero. `position_encoding` selects
`POSITION_NATIVE` (default), `POSITION_SI`, `POSITION_NORMALIZED_ZERO_ONE`, or
`POSITION_NORMALIZED_MINUS_ONE_ONE`. The separate `units` field applies only to
velocity and effort: NATIVE preserves existing driver settings; SI means physical
velocity and effort. JointState sets SI position encoding and SI velocity/effort
units explicitly, preserving joint name, frame, timestamp, and supplied fields.

STS3215 and stepper support native position/velocity combinations and SI
position-only commands (radians converted to ticks/degrees). They reject effort;
use presets to enable/disable torque. TI demo supports native position/velocity/
effort using its existing firmware scaling, but rejects SI commands. Drivers
validate the entire payload before writes. Channel failures are returned; a
multi-field command is not a transactional hardware operation.

Scalar topics retain native values: `/position` maps to position, `/speed` and
`/velocity` to velocity, `/effort` to effort. Legacy `/torque` on STS3215/stepper
maps to enable/disable presets; on TI demo it maps to native effort. `/dc` remains
unsupported by runtime motor drivers. The standalone Pybricks tool defines
native effort as duty percent and requires it to be sent alone.

Normalized positions map through configured operational limits once, then become
POSITION_NATIVE before driver execution. Nonfinite/out-of-range input and invalid
limits are rejected, never clamped. Native and SI positions are left for drivers.
Position encoding does not normalize velocity or effort.
Header metadata does not imply scheduling, clock synchronization, or transforms.

## Config migration

Replace `action { position: 2004 }` with:

```protobuf
action { joint { joint_name: "sts_motor_1" position: 2004 } }
```

The optional position marker may be written as `position_encoding: POSITION_NATIVE`. Replace
complex speed with `joint.velocity`; map physical/native effort only where the
driver supports it. Torque enable/disable becomes a preset, not effort.
Duration is no longer part of a command; trajectory waypoints provide timing.

Float32 trajectory publishing accepts one native numeric field, matching its
topic and joint name. Multi-field, SI, or mismatched payloads are rejected rather
than silently losing fields or units. The current Float32 trajectory publisher accepts native position encoding only.

Neither normalization nor position encoding belongs to Subscription. The actuator
node defines fixed contracts: numeric scalar/array position values are native;
JointState positions and velocity/effort are SI. The adapters populate JointCommand
encoding/units accordingly. Remove old `normalized` and `position_encoding` fields
from subscriptions; producers using normalized values must convert before ROS
publishing. Internal JointCommand retains its explicit position encoding, including
both normalized ranges for internal producers. Existing SI internal packets must
set POSITION_SI explicitly; `units: SI` applies only to velocity/effort.

Topic names are always strings. Message structure is selected by ros2_data_type:

```protobuf
node {
  id: 1
  node_type: ACTUATOR_SUBSCRIBER
  subscriptions {
    ros2_data_type: JOINT_STATE
    topic: "esp32_stepper_1/joint_state"
  }
}
```

The message's name/position/velocity/effort arrays belong in the ROS payload, not
in `topic`. The decoder selects the configured actuator's name and copies all
present numeric fields into one joint command. Other actuators may subscribe to
the same topic and select their own entries. The topic suffix does not infer the
message type. Drivers still reject unsupported field combinations.

Publishers must also implement the selected type. The position publisher supports
JointState position feedback; the current trajectory publisher supports only
Float32 and cannot publish combined position/velocity/effort by changing config.
Internal normalization conversion remains strict and uses operational limits;
ROS scalar inputs are never inferred to be normalized from their values.

The inference adapter's separate `ActionCommand.normalized` API is unchanged:
it converts its legacy [-1, 1] output with its existing clamp policy before ROS
publishing. That producer API does not use the removed packet/subscription flag.

## Extending support

Add compiled ROS message dispatch in `VisitPositionMessage`. New drivers consume
`joint` directly and define unit conversions, supported combinations, and limits.
Keep C++ topic aliases and Python `ACTION_TOPIC_SUFFIX_TO_FIELD` aligned. Perception
helpers are unchanged. Build/test execution is left to the operator.
