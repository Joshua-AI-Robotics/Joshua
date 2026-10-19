# Configuration validation MCP tool

`config_validation_server` exposes one read-only, zero-argument
`validate_config()` tool over stdio. The operator fixes the config path and
C++ validator executable when starting the server. The tool delegates parsing
and integrity checks to `//config:joshua_validate_config`; it does not select a
config, launch Joshua, initialize ROS, open declared devices, or certify
implementation support or hardware readiness. A valid report can describe
nonexistent hardware because this checks only the current config integrity
contract.

Build in the checkout on Ubuntu 22/Humble:

```sh
docker compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm -T joshua-u22 bazel build --config=u22 --config=x86-base --@rules_python//python/config_settings:python_version=3.10 //config:joshua_validate_config //tools/mcp:config_validation_server
```

Set the MCP client's command to `docker`, working directory to this checkout,
and arguments equivalent to:

```sh
compose -f docker-compose.yml -f docker-compose.cpu.yml run --rm -T -e ENABLE_MOCK_SERIAL_PORTS=false joshua-u22 /workspace/bazel-bin/tools/mcp/config_validation_server --validator /workspace/bazel-bin/config/joshua_validate_config --config /workspace/config/config_preset/so100/teleoperate.pbtxt
```

The config path must be visible inside the container under `/workspace`; choose
the intended file before starting the server. Replace `joshua-u22`, `u22`,
and Python `3.10` with `joshua-u24`, `u24`, and `3.12` for Jazzy. The
MCP adapter and C++ CLI report one bounded JSON object with a
`config_integrity` scope and the explicit limitation string. A completed
invalid config is a tool result (`isError=false`); missing input, adapter
errors, timeouts, and output failures are errors. The adapter permits one
active helper, caps each child pipe at 64 KiB, and waits up to ten seconds,
including process creation. It kills and reaps ordinary local children on
failure, cancellation, and shutdown. Verify the SDK stdio exchange in both
Docker stacks before claiming client compatibility.

The isolated pip locks are regenerated inside the matching Docker images with
an ephemeral venv and `pip==25.2 pip-tools==7.5.2`:

```sh
python3 -m venv /tmp/mcp-lock-env
/tmp/mcp-lock-env/bin/python -m pip install pip==25.2 pip-tools==7.5.2
/tmp/mcp-lock-env/bin/pip-compile --allow-unsafe --output-file=tools/mcp/requirements.lock tools/mcp/requirements.in
```

Use `requirements_3.12.lock` for the Python 3.12 run. Root and model lockfiles
are independent of this tool.
