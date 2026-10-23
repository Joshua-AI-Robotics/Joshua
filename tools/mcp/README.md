# Configuration validation MCP tool

`config_validation_server` exposes one read-only `validate_config` tool over
stdio. The operator fixes the C++ validator executable and a config root when
starting the server. No robot config is needed at startup. Each call selects a
relative `.pbtxt` reference beneath that root, for example:

```json
{"config_path":"so100/teleoperate.pbtxt"}
```

Build in the checkout on Ubuntu 22/Humble:

```sh
docker compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm -T joshua-u22 bazel build --config=u22 --config=x86-base --@rules_python//python/config_settings:python_version=3.10 //config:joshua_validate_config //tools/mcp:config_validation_server
```

Set the MCP client's command to `docker`, working directory to this checkout,
and arguments equivalent to:

```sh
compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm -T -e ENABLE_MOCK_SERIAL_PORTS=false joshua-u22 /workspace/bazel-bin/tools/mcp/config_validation_server --validator /workspace/bazel-bin/config/joshua_validate_config --config-root /workspace/config/config_preset
```

`--validator` is required and cannot be overridden by a tool call.
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
are errors. Verify the SDK stdio exchange in both Docker stacks before claiming
client compatibility.

The isolated pip locks are regenerated inside the matching Docker images with
an ephemeral venv and `pip==25.2 pip-tools==7.5.2`:

```sh
python3 -m venv /tmp/mcp-lock-env
/tmp/mcp-lock-env/bin/python -m pip install pip==25.2 pip-tools==7.5.2
/tmp/mcp-lock-env/bin/pip-compile --allow-unsafe --output-file=tools/mcp/requirements.lock tools/mcp/requirements.in
```

Use `requirements_3.12.lock` for the Python 3.12 run. Root and model lockfiles
are independent of this tool.
