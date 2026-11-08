# Read-only inspection MCP tools

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

`config_validation_server` exposes nine read-only tools over stdio when started
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

`list_supported_hardware` reads the bundled
[model catalog](../../docs/hardware_catalog.json), also rendered as
the [supported hardware guide](../../docs/SUPPORTED_HARDWARE.md). It is independent
of test history, the selected config root, factory enumeration, and runtime
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

`list_hardware_test_records` reads separate
[test records](../../docs/hardware_test_records.json), rendered as the
[test history guide](../../docs/TESTED_HARDWARE.md). Call with `{}` for all runs
or `{"hardware_id":"sunplus-spca2281-xwf-1080p"}` for the model's history.
`hardware_id` is an exact match. Results use the `hardware_test_history` scope,
with a `records` list containing run IDs, hardware references, dates,
Joshua commits, environments, evidence paths, measurements, and limitations.
Unknown IDs and models without tests return an empty list. Multiple runs can
reference one model; model metadata is not copied into individual runs. A new
test does not automatically add a model or change its intended support scope.
Broken model references produce `DATA_LOSS` rather than an apparent test result.

Both operations read metadata only and do not scan devices or establish current
connectivity, readiness, or compatibility of every revision. Each bundled file
and encoded report is bounded to 64 KiB; each file contains at most 128 entries.
Oversized reports return `RESOURCE_EXHAUSTED`, malformed data returns `DATA_LOSS`,
and missing data returns `UNAVAILABLE`. Model discovery remains available when
test history is missing or malformed. Requests cannot select metadata paths.
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

[`observation_source.py`](observation_source.py) provides a standalone, stdlib-only
`ObservationSource[T]` ABC and validated frozen metadata/result records. Concrete
adapters own payload validation, bounds and lifecycle enforcement. The
[design and usage examples](../../docs/MCP_OBSERVATION_DESIGN.md) describe the
implemented image adapter and remaining proposed adapters. The generic base
keeps the existing interfaces and response schemas.

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
[firmware](../../firmware/README.md) guides for wiring, firmware versions,
calibration and limits. New hardware requires implementation and verification;
see [AI integration](../../docs/AI_INTEGRATION.md) for the wider workflow and
remaining motion, commissioning, configuration-writing and task-execution gaps.

The isolated pip locks are regenerated inside the matching Docker images with
an ephemeral venv and `pip==25.2 pip-tools==7.5.2`:

```sh
python3 -m venv /tmp/mcp-lock-env
/tmp/mcp-lock-env/bin/python -m pip install pip==25.2 pip-tools==7.5.2
/tmp/mcp-lock-env/bin/pip-compile --allow-unsafe --output-file=tools/mcp/requirements.lock tools/mcp/requirements.in
```

Use `requirements_3.12.lock` for the Python 3.12 run. Root and model lockfiles
are independent of this tool.


## Optional configured camera image

Build `//ros2:image_observer` alongside the three MCP/inspection targets above.
The existing nine tools remain the default; enabling one image reader adds
`get_camera_image(camera_id)` as a tenth tool. Python does not import ROS,
OpenCV or a capture driver. The C++ launcher owns the camera process and device;
MCP starts only one passive C++ ROS helper and kills/reaps that helper on close.

After the operator confirms the camera is connected and ready, read the entire
camera preset and all endpoints, then use the normal launcher with
`--observation_dir=<private-directory> --observe_camera=webcam` for
[`publish_camera.pbtxt`](../../config/config_preset/example/publish_camera.pbtxt).
Create the mode `0700` shared directory using the same UID and filesystem mount
as both processes, as described above. The selected sensor must be IMAGE with
OpenCV settings, on a CAMERA_PUBLISHER node containing exactly one IMAGE sensor
and exactly one IMAGE publisher. Other cameras can use separate node IDs.
Competing normalized topic publishers, invalid runtime options, and publication
rates outside 1..1000 Hz are rejected before launch. Simulation is unsupported.

Add these fixed MCP startup arguments to the command above:

```sh
--observation-dir /workspace/private-observations --image-observer /workspace/bazel-bin/ros2/image_observer --camera-id sensor:webcam
```

Call `get_camera_image` with `{"camera_id":"sensor:webcam"}`. Requests cannot
choose a device, topic, domain, executable, resolution or codec. The configured
ID is enabled even when the runtime is absent: it reports `unavailable` until
an associated sample arrives. A fresh result includes exactly one standard MCP
`image/jpeg` image block, plus bounded text/structured metadata. A stale result
contains metadata only. Unavailable/error results contain no image. A compatible
MCP host owns displaying the image or providing it to its vision model; this
tool performs no inference.

The private `camera-source.json` is derived launcher metadata under the existing
producer lease, separate from unchanged `snapshot.json`. Its binding includes
launcher instance, configured ID, effective ROS domain, normalized topic,
expected node/namespace and fixed ROS Image type. A changed binding invalidates
all images, reaps the old helper and creates a new ROS context on retry. A reply
racing a detected binding change is unavailable. The helper verifies the sole
expected graph endpoint and actual received publisher GID at sample acceptance,
and validates the endpoint again on query. Missing, wrong, multiple or replaced
publishers clear the sample; metadata association does not authenticate hardware.

Limits are fixed: one camera/helper/query, five-second read deadline including
spawn/manifest/discovery/JPEG work, and one two-second cleanup budget. Failed
cleanup permanently disables further reads until MCP restarts; late spawns retain
FD/PID ownership and get a best-effort reaper. Manifest and image metadata are
at most 16 KiB each, helper stderr at most 64 KiB, accepted raw RGB8/BGR8 frames
at most 1920×1080 and 8 MiB with checked row stride. One sample is retained.
JPEG is aspect preserving, never upscaled, at most 640×480, quality 80 and
512 KiB. The entire serialized MCP tool result is checked against 1 MiB,
including base64, metadata, text and JSON escaping. Other text reports retain
their 64 KiB limit. These accepted-data limits cannot prevent DDS allocations
before the callback and do not establish a strict process memory bound.

Comparable boot/time-namespace CLOCK_BOOTTIME receipt and manifest freshness is
three seconds. Rereads preserve original receipt time, source publication time,
and the helper nonce/counter `sample_id`; ROS Image has no native sequence, so
`sample_sequence` is null. Publication time is labeled `publication_time`, and
`acquisition_freshness` stays `unknown`.

Capture settings are applied on successful first open and reopen. FourCC is
required, must contain four printable ASCII bytes, and is applied before the
resolution and frame rate. Two capture buffers preserve continuous acquisition. Reported image dimensions are actual frame dimensions, not requested
settings. Finite capture retries/backoff do **not** bound synchronous OpenCV
open/read/release calls: a stalled camera process may require operator recovery.
The observer remains bounded and a silent, still-present publisher becomes stale;
MCP never restarts or reconfigures the camera process.

The synthetic ROS fixture → production helper → typed adapter → actual SDK
stdio test decodes JPEG pixels and checks source/freshness, replacement,
disconnect/recovery, silent-publisher staleness, padding and domain/topic/runtime
changes. Child-process tests cover framing, cancellation, close, timeout and
cleanup failure. Physical webcam unplug/recovery and a chosen AI client's
image display/analysis remain separate acceptance evidence requiring current
operator readiness confirmation.
