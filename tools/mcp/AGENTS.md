# MCP adapters

Follow the repository [AGENTS.md](../../AGENTS.md) and
[coding guidelines](../../AGENTS.md#coding-guidelines). This file owns the rules and architecture
contract for `tools/mcp/`; child directories inherit it unless they add local rules.

## Architecture contract

This directory owns bounded stdio adapters and read-only observation readers.
C++ parsing/`ValidateConfig` owns config acceptance; factory tables own software
discovery; curated model support and historical test records remain distinct.
NodeManager owns runtime processes and snapshot publication. MCP consumes those
contracts through thin adapters and never initializes drivers, commands devices,
launches/stops a robot, or turns missing/stale observations into permission to move.

Keep tool-selected paths confined to the configured root. Tool arguments cannot
choose helper executables or metadata sources. Preserve bounded input/output,
helper deadlines, cancellation/reaping, and one-helper admission behavior.
Runtime snapshots preserve source identity, receipt freshness, and provenance
limitations. Do not equate process readiness, protocol acknowledgment, historical
hardware evidence, or publication timestamps with current physical readiness.

## Verification

Use the existing SDK stdio, helper-process, path-confinement, observation, and
catalog tests in [BUILD](BUILD). Exercise malformed/oversized data, cancellation,
missing/stale sources, and independent model/test-history failures. Run the
changed `//tools/mcp:..._test` targets in both Docker test stacks. Follow the
AGENTS.md for setup smoke checks; do not launch an operator's robot to test an adapter.

## Read-only inspection MCP tools

## Local preparation and launch

Start with `./scripts/joshua_mcp.sh setup` for guided preparation and AI IDE
registration. See [MCP setup](../../docs/MCP_SETUP.md) for prerequisites, generic
client settings and recovery. The commands below remain useful for development.

Run `./scripts/joshua_mcp.sh prepare`, then `./scripts/joshua_mcp.sh check`
from the repository root. Use the absolute path to `scripts/joshua_mcp.sh` with
argument `serve` as a stdio client command. `--stack u24` selects Jazzy;
`--config-root /absolute/workspace/config` selects the actual configuration
directory to inspect. The default is this checkout's `config/config_preset`.

This CPU-only launcher retains complete artifacts and pins their image and
local Docker daemon. It needs no GPU, host ROS/Bazel, hardware mounts or robot
launch. Client connection never compiles or installs dependencies. Use `doctor`
for diagnostics; after source changes, prepare again and reconnect the client.
The manual Bazel commands below remain available for development.

`config_validation_server` exposes ten read-only tools over stdio when started
with `--inspector`. `list_supported_components()` discovers factory-owned build
metadata without a config. `list_supported_hardware(kind=None, usb_id=None, query=None)`
searches curated model support metadata. `list_hardware_test_records(hardware_id=None)`
retrieves separate historical test runs. Neither requires a candidate config.
`validate_config(config_path)`,
`list_devices(config_path)` and `describe_device(config_path, device_id)` inspect
candidate protobuf files beneath an operator-selected root. One session can
inspect several candidates. For example:

```json
{"config_path":"so100/teleoperate.pbtxt"}
```

Build in the checkout on Ubuntu 22/Humble:

```sh
docker compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm -T joshua-u22 bazel build --config=u22 --config=x86-base --@rules_python//python/config_settings:python_version=3.10 //config:joshua_validate_config //config:joshua_inspect_config //tools/mcp:config_validation_server
```

Set the MCP client's command to `docker`, working directory to this checkout,
and arguments equivalent to:

```sh
compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm -T -e ENABLE_MOCK_SERIAL_PORTS=false joshua-u22 /workspace/bazel-bin/tools/mcp/config_validation_server --validator /workspace/bazel-bin/config/joshua_validate_config --inspector /workspace/bazel-bin/config/joshua_inspect_config --config-root /workspace/config/config_preset
```

`--validator` is required. The executable paths cannot be overridden by a tool
call. Starting without `--inspector` and `--observation-dir` preserves the
validation-only one-tool interface.
`--config-root` is optional and defaults to the server's startup working
directory. The server opens and pins the root directory at startup, so renaming
or replacing its original pathname does not redirect subsequent calls. The
server need not find any particular config until that config is requested.
Use `joshua-u24`, `u24`, and Python `3.12` for the Jazzy stack.

`config_path` is exactly one UTF-8 string of at most 4096 bytes. It must be a
relative POSIX path with nonempty components, no `.` or `..`, and no NUL.
Requests cannot select the validator or root. The helper rejects symlinks in
any requested component, even when they point back inside the root. Missing or
inaccessible files return bounded input errors; malformed protobuf returns a
completed validation result with a bounded, one-based parser location. The
public report keeps the exact accepted relative reference.

The root tree is operator managed. Descriptor-relative nofollow lookup confines
request traversal; it is not a sandbox against hard links, mount changes,
operator-controlled directory relocation, or concurrent writes to an already
opened file. The C++ parser and `ValidateConfig` remain the authorities. The
tool does not launch Joshua, initialize ROS, open devices declared by a config,
or certify implementation support or hardware readiness. An empty config
retains the current validator's valid result.

The adapter permits one helper at a time, caps each stdout and stderr pipe at
64 KiB, and enforces a ten-second helper deadline including process creation
and file access. It kills and reaps ordinary local children on failure,
cancellation, and shutdown. If cleanup cannot finish within its two-second
budget, the service refuses further validation with `UNAVAILABLE` and an
operator restart instruction; an ordinary overlapping request receives
`RESOURCE_EXHAUSTED`. A completed invalid config is a tool result
(`isError=false`); missing input, adapter errors, timeouts, and output failures
are errors. The tests exercise the actual SDK stdio exchange in both ROS stacks.

## Catalog and device reports

`query_hardware_catalog(view="history", hardware_id="ti-lp-am243", limit=20)`
reads complete inline tests from the [device catalog](../../docs/hardware_catalog/AGENTS.md).
The default view is bounded device summaries; `view="history"` returns tests with
embedded setups, criteria, results, evidence and attribution. Optional filters are
`hardware_id`, `kind`, `query`, `result` (history only), and `validated_by` (history
only). Continue with `next_cursor` and the same filters. A large entry returns an
explicit size error; its complete device source remains available.

Each device/board has one authoritative JSON file. Add a device by adding that
file; append tests in that same file. No separate metadata or generated pages need
updating. The original two APIs below preserve their v1 camera compatibility views
by projecting from the same device files at query time.

`list_supported_hardware` reads the bundled
[device catalog](../../docs/hardware_catalog/), linked from
the [supported hardware guide](../../docs/SUPPORTED_HARDWARE.md). It requires no selected config root, factory enumeration, or runtime
observations. Call with `{}` for all models,
`{"kind":"camera","query":"XWF-1080P"}` for the
Sunplus camera, or `{"query":"SPCA2281"}` for an alias search.
`usb_id` is a case-insensitive, exact USB VID:PID match for records containing
that identifier; the Sunplus record uses model and aliases instead.
`query` is a case-insensitive substring of model, manufacturer, aliases, record
ID, driver, or identifier values. `kind` is an exact match. Filters combine
with AND. Omit unused filters; nulls and unknown arguments are rejected.
No match is an OK report with an empty `hardware` list. Model results use the
`hardware_catalog` scope and include a stable hardware ID, aliases, driver,
example preset, and intended support scope. No measurements are embedded in
model entries. Absence does not prove incompatibility with an existing driver.

`list_hardware_test_records` derives separate query results from
[device validation history](../../docs/hardware_catalog/), rendered as the
[test history guide](../../docs/TESTED_HARDWARE.md). Call with `{}` for legacy camera runs
or `{"hardware_id":"sunplus-spca2281-xwf-1080p"}` for the model's history.
`hardware_id` is an exact match. Results use the `hardware_test_history` scope,
with a `records` list containing run IDs, hardware references, dates,
Joshua commits, environments, evidence paths, measurements, and limitations.
Unknown IDs and models without tests return an empty list. Multiple runs can
reference one model; model metadata is not copied into individual runs. A new
test does not automatically add a model or change its intended support scope.
Broken model references produce `DATA_LOSS` rather than an apparent test result.

Both operations read metadata only and do not scan devices or establish current
connectivity, readiness, or compatibility of every revision. Encoded v1 reports
are bounded to 64 KiB and 128 entries. The shared device loader admits at most
256 KiB per device and 32 MiB overall. Oversized reports return
`RESOURCE_EXHAUSTED`, malformed data returns `DATA_LOSS`, and missing data returns
`UNAVAILABLE`. Default projections validate the same device sources, so malformed
history can also prevent model discovery. Requests cannot select metadata paths.
The software catalog below and existing validation/inspection report schemas
retain their meanings.

Factory implementation metadata comes from the same fixed C++ tables used to construct boards,
transports, motors and sensors. Enumeration calls no creator or driver.
`installed`, `test_only`, `demo_only` and `unimplemented` describe software
selection, with transport configuration requirements attached to each entry.
They do not certify a particular combination or physical hardware. `MOCK` is
only C++ test infrastructure. The retired TI-demo motor and standalone EtherCAT
cyclic transport are not listed; JoshuaWire EtherCAT exposes paired message and
cyclic transport with explicit endpoint and timing requirements.

Device identities are `board:<name>`, `actuator:<name>` or `sensor:<name>`.
Inventory includes at most 128 entries with identities of at most 512 UTF-8
bytes and reports the omitted count. A description searches the complete
candidate and rejects an absent or ambiguous identity. Reports include declared
configuration, topics, references and limits, with compatibility and hardware
readiness explicitly unverified. Helper output and each MCP text report are
capped at 64 KiB, including Unicode escaping; a larger report returns
`RESOURCE_EXHAUSTED`. The session remains usable for subsequent requests.

## Typed observation contract

[observation_source.py](observation_source.py) implements the standalone,
stdlib-only ABC and validated metadata records. It adds no MCP tool or production
adapter. Existing setup and runtime-observation tools retain their schemas.

### Reader interface

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

### Metadata and results

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
or independent owned copies. ROS adapters must preserve verified
domain/node/publisher association before reporting data; required but unverifiable
association makes a source unavailable. Such association is not physical hardware
authentication. Transport-specific provenance records remain deferred.

### Usage with a fake

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
[test-only cooperative fake](observation_source_test.py), not a
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

[observation_source_test.py](observation_source_test.py) verifies metadata,
incomplete-ABC rejection, and SDK/ROS-free imports. Its cooperative fake covers
sample identity, freshness, errors, overlap, deadline cleanup, caller cancellation,
concurrent close, and read-after-close. Those tests establish the fake's behavior;
new adapters must independently verify their lifecycle and payload obligations.
Camera, audio, and composed position readers are not implemented by this ABC.

## Optional runtime observations

`get_runtime_status()`, `get_recent_logs(cursor=None, limit=50, min_level="WARN")`
and `get_feedback(sensor_id)` read an operator-selected private snapshot
directory. Without `--observation-dir`, these tools report `UNAVAILABLE`.
The launcher and NodeManager own runtime processes; adapter `aclose()` only
closes observation descriptors. MCP never launches, stops or commands a robot.

For an operator-authorized runtime, create a directory owned by its UID with
mode `0700` on an ordinary local filesystem, then add `--observation_dir=<directory>` to the normal launcher
arguments. Optionally select one configured POSITION encoder with
`--observe_sensor=<sensor_name>`. It must use POSITION_PUBLISHER with exactly
one JOINT_STATE publisher topic and no other configured publisher on that
topic. Observation settings and the exclusive directory lease are checked
before children launch. Simulation observation is currently unsupported.
Starting a real preset requires the current hardware confirmation in
[AGENTS.md](../../AGENTS.md).

Start MCP separately with `--observation-dir <directory>` pointing at that same
directory in the same Linux boot and time namespace. Container mounts must
preserve the directory and UID. Default Compose containers run as root; create
the shared directory from that same container UID or explicitly match the
runtime and reader UIDs. A host-owned `0700` directory will not work with the
default root container. Files and directories must be private; symlinks,
special files, multiple hard links and snapshots larger than 64 KiB are rejected.
Atomic snapshots use CLOCK_BOOTTIME, a runtime instance ID and a sequence.
`--freshness-seconds` defaults to 3; old snapshots remain visible as `stale`.
Publication follows the roughly one-second process-monitoring cadence with a
bounded ROS spin budget; filesystem and DDS work have no hard realtime
guarantee.
The config provenance is the launcher's loaded config; children can reload its
path, so no identical-content claim is made.

Status reports process facts, retained exits and observation failures. ROS logs
are bounded partial `/rosout` history for configured logger names; those names
do not prove runtime provenance (`runtime_association_unverified`). Cursors
include instance and sequence, report retention gaps, reset on a new instance
and reject future sequence numbers. Invalid source timestamps/severities are
discarded; drop counts also include retention and encoded-size evictions.
Filtering advances across examined records
without skipping a later matching page.

Feedback requires the sole expected graph publisher and the actual message's
publisher GID to match. Missing, wrong, multiple or changed publishers invalidate
the cached sample. Measurements preserve source stamps and receipt age;
publication time does not establish acquisition freshness. Position is radians
relative to device zero and may measure a leader sensor. It does not establish
controlled-actuator position, tool accuracy, connectivity, readiness or task
completion. Missing observations are not permission to move.

Use protobuf presets for hardware declarations and the relevant subsystem and
[firmware](../../firmware/AGENTS.md) guides for wiring, firmware versions,
calibration and limits. New hardware requires implementation and verification;
configuration writing, motion, commissioning, and general task execution are
not capabilities of this read-only adapter.

The isolated pip locks are regenerated inside the matching Docker images with
an ephemeral venv and `pip==25.2 pip-tools==7.5.2`:

```sh
python3 -m venv /tmp/mcp-lock-env
/tmp/mcp-lock-env/bin/python -m pip install pip==25.2 pip-tools==7.5.2
/tmp/mcp-lock-env/bin/pip-compile --allow-unsafe --output-file=tools/mcp/requirements.lock tools/mcp/requirements.in
```

Use `requirements_3.12.lock` for the Python 3.12 run. Root and model lockfiles
are independent of this tool.

## Integration boundary

Before MCP exposes an operation, its implementing subsystem must define and test
inputs, outputs, state transitions, cancellation, errors, observability, and limits.
Adapters use that established interface, including ROS topics/services/actions
where appropriate, rather than adding another runtime or opening hardware drivers.
Joshua must continue to work through protobuf configuration and the normal launcher
without MCP. Capability claims must match merged implementations and their recorded
evidence; configured components do not imply a verified composed task.

## Operator requests through Decisions

An MCP client sends each whole Joshua turn to `request_decision(text, sequence)`.
Decisions chooses a capability. Joshua validates the choice and confidence,
reads runtime status locally or asks Responses to write a general answer, and
returns the route, outcome, reply and per-call usage. The transcript belongs
to the client. Each stdio connection starts at sequence 1, retains four recent
turns and accepts one active request. A retry of the last completed sequence
and text returns the cached result. MCP cancellation consumes that sequence.
The 20-second local monotonic deadline bounds network wait independently of
RobotTime. Current messages and bounded conversational history go to OpenAI.
Raw runtime snapshots stay with the observation reader.

Run `./scripts/joshua_mcp.sh prepare` once after checking out this revision.
Register a stdio server in the MCP client with the absolute path to
`scripts/joshua_mcp.sh` as its command and `serve-decisions` as its argument.
Give that server process `OPENAI_API_KEY` through its environment. Its
prepared Docker service can reach OpenAI and mounts the generation read-only.
Add `--observation-dir /absolute/private/snapshot-directory` to read an
operator-selected runtime snapshot. A missing or stale snapshot yields an
unavailable status. The inspection entry point is documented under
[local preparation and launch](#local-preparation-and-launch).

In a Docker dev shell, run `bazel test //tools/mcp:decisions_request_test`.
