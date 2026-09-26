# Packet parser

`packet_parser.cc` bridges compiled ROS message types and internal packets.
`packet_parser.py` supplies matching field access for Python consumers.

`ActionPacket.joint` is the motion payload for every actuator. Optional position,
velocity, and effort distinguish omission from zero. `units: NATIVE` is the
default: position uses existing driver units and velocity retains the driver's
nonnegative move-speed setting. `units: SI` represents physical position,
velocity, and effort. ROS JointState decoding sets SI explicitly and preserves
joint name, frame, timestamp, and every supplied numeric field.

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

Normalized scalar positions map through configured operational limits and clear
the normalization flag before driver execution. SI commands cannot be normalized.
Header metadata does not imply scheduling, clock synchronization, or transforms.

## Config migration

Replace `action { position: 2004 }` with:

```protobuf
action { joint { joint_name: "sts_motor_1" position: 2004 } }
```

The optional native units marker may be written as `units: NATIVE`. Replace
complex speed with `joint.velocity`; map physical/native effort only where the
driver supports it. Torque enable/disable becomes a preset, not effort.
Duration is no longer part of a command; trajectory waypoints provide timing.

Float32 trajectory publishing accepts one native numeric field, matching its
topic and joint name. Multi-field, SI, or mismatched payloads are rejected rather
than silently losing fields or units. Match the publisher/subscriber normalized
position convention in config.

Removed protobuf field numbers/names are reserved; `normalized` and `preset`
retain their existing wire numbers. `joint` has a new tag because its default
units differ from the retired SI-only payload. Old scalar/complex binary
packets and old text configs require explicit migration and producer/consumer
updates together; they are not automatically translated.

## Extending support

Add compiled ROS message dispatch in `VisitPositionMessage`. New drivers consume
`joint` directly and define unit conversions, supported combinations, and limits.
Keep C++ topic aliases and Python `ACTION_TOPIC_SUFFIX_TO_FIELD` aligned. Perception
helpers are unchanged. Build/test execution is left to the operator.
