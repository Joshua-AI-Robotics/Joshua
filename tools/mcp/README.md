# Read-only inspection MCP tools

`config_validation_server` exposes seven read-only tools over stdio when started
with `--inspector`. `list_supported_components()` discovers factory-owned build
metadata without a config. `validate_config(config_path)`,
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

Catalog metadata comes from the same fixed C++ tables used to construct boards,
transports, motors and sensors. Enumeration calls no creator or driver.
`installed`, `test_only`, `demo_only` and `unimplemented` describe software
selection, with transport configuration requirements attached to each entry.
They do not certify a particular combination or physical hardware. `MOCK` is
only C++ test infrastructure; TI demo support retains its demo scope.

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
[design and usage examples](../../docs/MCP_OBSERVATION_DESIGN.md) distinguish the
implemented base from deferred adapters. This library exposes no new MCP tool
and does not change the existing interfaces or response schemas.

## Optional runtime observations

`get_runtime_status()`, `get_recent_logs(cursor=None, limit=50, min_level="WARN")`
and `get_feedback(sensor_id)` read an operator-selected private snapshot
directory. Without `--observation-dir`, these tools report `UNAVAILABLE`.
The launcher and NodeGenerator own runtime processes; adapter `aclose()` only
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
