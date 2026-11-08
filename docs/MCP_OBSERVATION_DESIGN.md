# Observation sources for MCP

**Status: basic contract and metadata implemented; adapters remain proposed.**
[observation_source.py](../tools/mcp/observation_source.py) defines a reusable
Python reader contract. This increment adds no MCP tool or concrete adapter.
The existing setup inspection, runtime status, partial ROS logs and selected
position feedback in draft PR #130 retain their current interfaces and schemas.
Camera retrieval remains deferred.

## Goal and placement

Give diverse inputs a consistent reader interface while preserving their meaning.
Device settings remain in protobuf configuration. Another camera can reuse an
image adapter; a different measurement type can need another typed adapter.

Joshua's C++ [PerceptionInterface](../robot/perception/interfaces/perception_interface.h)
and [PerceptionPacket](../robot/perception/proto/perception_packet.proto) remain
the hardware contracts. Future readers sit above tested backend operations:

```mermaid
flowchart LR
    D[Existing C++ drivers] --> R[Joshua runtime and ROS 2]
    R --> B[Tested backend observation operations]
    B --> O[ObservationSource: one bound input]
    O --> M[Typed MCP tools]
```

The launcher owns processes; backends own ROS access and validation. Reader close
releases query resources without stopping a robot or camera. A future adapter can
compose behind [RuntimeObserver](../tools/mcp/interfaces.py); this increment
does not migrate that interface or the existing snapshot reader. SetupInspector
continues describing configured components.

## Small abstract contract

`ObservationSource[T]` has exactly three abstract methods:

| Method | Adapter obligation |
|---|---|
| `describe() -> SourceDescriptor` | Return stable cached metadata, including after close, without discovery or device access. |
| `async read_latest() -> ObservationResult[T]` | Non-destructively return the latest bounded sample or explicit unavailability; repeated reads can return the same sample. |
| `async aclose() -> None` | Idempotently release reader resources and source-owned queries; concurrent calls are safe. |

Close cancels source-owned query work. Reads still pending when closure wins return
`UNAVAILABLE/CLOSED`; subsequent reads stay closed. An already completed read is
not retroactively changed. Cancellation of a caller's read task propagates after
query cleanup; closing a source must not cancel consumer-owned tasks.

**The ABC implements no lifecycle engine.** Concrete adapters enforce deadlines,
concurrency, payload validation and byte bounds, source binding, clock
comparability, freshness, and cleanup. A subclass that merely implements the
three method names has not proved those obligations. Reusable transport/codec
helpers can be composed outside the base when real adapters need them.

Bind policy at reader creation; requests cannot create unlimited helpers or
subscriptions. Configured IDs select operator-enabled readers, excluding arbitrary
model-selected ROS topics, paths or endpoints. One device can expose multiple
sources, such as image and audio.

## Metadata and results

Four frozen records describe the contract:

| Record | Fields and meaning |
|---|---|
| `ObservationLimits` | Positive finite `read_timeout_seconds` and `freshness_seconds`; positive integer `max_payload_bytes` and `max_in_flight_reads`. Defaults: 10 seconds, 3 seconds, 64 KiB and one read. |
| `SourceDescriptor` | Required `source_id`, `kind`, `schema_name`; optional `encoding`, `units`, `reference`; default limits. References configured identity without copying wiring or calibration. |
| `ObservationTime` | Integer `nanoseconds` and optional `clock_id`. Signed source time, including zero, is valid; `None` means unknown clock origin. |
| `ObservationResult[T]` | Required source ID and `Availability`; optional typed payload, receipt/source times, runtime instance, sequence, diagnostic and `UnavailableReason`. |

`FRESH` and `STALE` require a payload and a receipt time with a known clock ID;
they prohibit an unavailable reason. `UNAVAILABLE` requires no payload, a
nonempty human diagnostic and one stable reason: `NO_DATA`, `BUSY`, `TIMEOUT`,
`CLOSED`, `INVALID_DATA` or `BACKEND_ERROR`. These codes describe outcomes;
they do not promise that retry is safe. Callers never need to parse diagnostics
to identify the outcome.

Constructors enforce these metadata invariants, exact integer types (excluding
bool), and optional nonnegative sequence numbers. Text is UTF-8 without NUL,
at most 512 bytes per field; diagnostics allow 4096 bytes and can be empty for
available results. Optional text uses `None` when unknown; supplied identity,
format and clock text must be nonempty. The limits are policies for adapters to
enforce, not universal media defaults or automatic runtime bounds.

A reread preserves backend sample identity, sequence and receipt/source timestamps.
Unknown sequence or runtime identity stays `None`. Receipt freshness does not
establish acquisition freshness. Clock comparisons require a known comparable
origin, including boot/time namespace where applicable; the generic layer never
subtracts unrelated timestamps. A nonempty clock label alone verifies no
synchronization.

`Generic[T]` is a static payload contract, not runtime validation. Adapters
validate actual payload type, representation size, units and source binding.
Frozen records do not deep-freeze arbitrary payloads: supply immutable payloads
or independent owned copies. Future ROS adapters must preserve verified
domain/node/publisher association before reporting data; required but unverifiable
association makes a source unavailable. Such association is not physical hardware
authentication. Transport-specific provenance records remain deferred.

## Usage with a fake

Constructing metadata needs only the descriptor's three identifying fields.
An available sample additionally needs its payload and original known receipt
time. This example uses an invented clock and opens no device:

```python
from dataclasses import dataclass

from tools.mcp.observation_source import (
    Availability, ObservationResult, ObservationTime, SourceDescriptor,
)

@dataclass(frozen=True)
class Reading:
    value: float

descriptor = SourceDescriptor("sensor:example", "numeric", "example.numeric/v1")
sample = ObservationResult(
    source_id=descriptor.source_id,
    availability=Availability.FRESH,
    payload=Reading(2.5),
    receipt_time=ObservationTime(1_000_000_000, "fake-boot:fake-namespace:monotonic"),
)
```

A caller uses the same methods for either payload kind. The following uses the
[test-only cooperative fake](../tools/mcp/observation_source_test.py), not a
production adapter. The fixture supplies its known immutable sample metadata.

```python
import asyncio

from tools.mcp.observation_source import Availability, ObservationSource
from tools.mcp.observation_source_test import FakeObservationSource, NumericReading

async def inspect(source: ObservationSource[NumericReading]) -> None:
    try:
        print(source.describe())
        first = await source.read_latest()
        second = await source.read_latest()  # Can repeat the same sample identity.
        if second.availability is Availability.UNAVAILABLE:
            print(second.unavailable_reason, second.diagnostic)
        else:
            print(first.sample_sequence, second.sample_sequence, second.payload)
    finally:
        await source.aclose()

asyncio.run(inspect(FakeObservationSource(NumericReading(2.5), lambda: 1_000_000_000)))
```

## Deferred adapters and scaling

These responsibilities are future work, not empty classes to scaffold now:

| Proposed adapter | Payload and role | When to add it |
|---|---|---|
| `JointPositionSource` | Position, units, zero/reference and timestamps | First real adapter; reuse the existing snapshot reader |
| `ImageSource` | Bounded image, dimensions, encoding and timing | When image retrieval is approved |
| `AudioSource` | Bounded chunk, rate, channels, sequence and discontinuities | With an audio workflow |
| `RangeScanSource` | Ranges/point-cloud data, frame and units | With a verified matching backend |
| `InputStateSource` | Defined button/axis state | With an implemented input path/schema |

An IMU likewise needs a verified backend, beyond an enum or schema.
`read_latest()` provides snapshots/current state and can repeat a sequence.
Waiting for new samples, every audio sample/button edge, recording, streaming and
synchronized groups need separate contracts.

Choose on-demand queries or bounded persistent subscriptions using measured
frequency, latency and resource needs. Avoid a worker per device by default;
keep heavy media work outside monitoring/control callbacks. This increment
contains no system scalability benchmark or production enforcement engine.

Typed MCP tools can later convert numeric results to structured data and images to
standard [MCP image content](https://modelcontextprotocol.io/specification/2025-11-25/server/tools#image-content).
MCP objects stay outside the base; recognition belongs to a client or separately
designed inference capability.

## Related projects and design lessons

Checked 2026-10-22 (America/Los_Angeles): maintainer docs/source reviewed,
external projects not executed; no scalability claim.

| Project | Reviewed pattern and Joshua lesson |
|---|---|
| LeRobot | [Camera ABC](https://github.com/huggingface/lerobot/blob/f9e9d739ea7055b282ea1b165e8785412a674693/src/lerobot/cameras/camera.py), disconnected [observation metadata](https://github.com/huggingface/lerobot/blob/f9e9d739ea7055b282ea1b165e8785412a674693/src/lerobot/robots/robot.py) and [OpenCV peeking](https://github.com/huggingface/lerobot/blob/f9e9d739ea7055b282ea1b165e8785412a674693/src/lerobot/cameras/opencv/camera_opencv.py): reuse descriptions and snapshot semantics; retain hardware ownership below MCP. |
| ros2_control | [SensorInterface](https://github.com/ros-controls/ros2_control/blob/67646cfe21853fe3a66386b106daed1853dc5197/hardware_interface/include/hardware_interface/sensor_interface.hpp) separates state from commands but owns hardware lifecycle. Keep Joshua's C++ drivers authoritative. |
| ROS MCP Server | [Generic topic/schema discovery and subscriptions](https://github.com/robotmcp/ros-mcp-server/blob/476591ac058f58cae810cc775e50b51f8d3d757c/ros_mcp/tools/topics.py): reuse retrieval/presentation separation; bind Joshua readers to configured enabled sources. |
| ROS 2 image_transport | [Decoded-image callback](https://github.com/ros-perception/image_common/blob/jazzy/image_transport/include/image_transport/subscriber_plugin.hpp) omits `MessageInfo`. Preserve verified association across transport adaptation. |
| Foxglove SDK | [Typed/custom-schema channels](https://docs.foxglove.dev/docs/sdk/concepts) organize messages. Describe schemas explicitly; validate source association separately. |

## Acceptance and delivery

Implemented basic-contract checks are in
[observation_source_test.py](../tools/mcp/observation_source_test.py).
Production tests establish metadata invariants, incomplete-ABC rejection and
SDK/ROS-free import. One cooperative generic fake and two frozen payload kinds
provide executable conformance examples: repeated identity/timestamps,
fresh-to-stale transition, missing/invalid data, typed reasons, bounded overlap,
deadline cleanup, caller cancellation, concurrent close and read-after-close.
Those examples prove the fake's behavior, not arbitrary subclasses' enforcement.

Future adapter increments must independently verify real payload validation and
ownership, deadlines/concurrency/cleanup, source association, restart and
clock/freshness behavior. Preserve all seven MCP tools, validation-only mode and
response schemas. Real media needs malformed/oversized and decodability checks;
ROS adapters need domain isolation, publisher replacement and hidden endpoint
evidence tested through actual SDK exchanges on both supported ROS stacks.

Camera, audio, position composition and other concrete sources remain deferred.
Observation alone never proves physical readiness or task completion;
[AI integration requirements](AI_INTEGRATION.md#requirements) govern those workflows.
