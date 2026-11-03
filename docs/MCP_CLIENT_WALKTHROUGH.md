# Inspect a robot from Codex

This walkthrough answers “What can this robot do?” and “Is this configuration
valid?” using the real Joshua MCP server. It inspects the checked-in SO-100
configuration without connecting hardware or launching a robot.

## Connect the client

Use the prepared Ubuntu 22/Humble Docker environment and build the inspector,
validator and server as described in the [MCP guide](../tools/mcp/README.md).
Follow [Codex MCP setup](https://developers.openai.com/codex/mcp) to add this
entry to an isolated project configuration, replacing both absolute paths:

```toml
[mcp_servers.joshua]
command = "docker"
cwd = "/absolute/path/to/Joshua"
args = [
  "compose", "-f", "docker-compose.yml", "-f", "docker-compose.cpu.yml",
  "run", "--rm", "-T", "-e", "ENABLE_MOCK_SERIAL_PORTS=false",
  "-v", "/absolute/path/to/candidates:/inspection-configs:ro",
  "joshua-u22", "/workspace/bazel-bin/tools/mcp/config_validation_server",
  "--validator", "/workspace/bazel-bin/config/joshua_validate_config",
  "--inspector", "/workspace/bazel-bin/config/joshua_inspect_config",
  "--config-root", "/inspection-configs",
]
enabled_tools = ["describe_robot_capabilities", "validate_config"]
startup_timeout_sec = 60
```

From the checkout, prepare disposable candidates with this command. Use its
printed directory as the candidate path above; the checked-in preset is unchanged.

```sh
python3 - <<'PYTHON'
from pathlib import Path
import tempfile

root = Path(tempfile.mkdtemp(prefix="joshua-inspection-"))
(root / "so100").mkdir()
text = Path("config/config_preset/so100/teleoperate.pbtxt").read_text()
(root / "so100/teleoperate.pbtxt").write_text(text)
(root / "malformed.pbtxt").write_text("robot {\n")
(root / "invalid.pbtxt").write_text(text.replace(
    'board_name: "arm_bus"', 'board_name: "missing_bus"', 1))
print(root)
PYTHON
```

Start a fresh Codex conversation with the server enabled. Ask these prompts
one at a time:

1. “What can the robot in `so100/teleoperate.pbtxt` do? Explain its position
   parameters, units, limits, prerequisites, and how movement could be verified.”
2. “Is that configuration valid? Also validate `malformed.pbtxt` and
   `invalid.pbtxt`, and explain the difference.”
3. “Can this setup pick and place objects, execute a verified movement through
   MCP, or use its leader readings to prove the follower moved?”

The server accepts one helper operation at a time. A concurrent request may
return `RESOURCE_EXHAUSTED` with “Validation is already running.” Retry after
the active request finishes. `UNAVAILABLE` with a restart instruction is a
different condition and requires operator action.

## Recorded result

A real Codex CLI 0.156.1 session was run on 2026-11-01 against implementation
commit `ed727630fbf064660d10e4d61f13020fa286dd70`, using the actual Docker-backed
stdio server and copied public configurations. Only the two inspection tools
were enabled; shell, browser and other agent tools were disabled. The CLI's
default model was used; its model identifier was not reported in the JSON event
stream. This evidence does not establish behavior in every client or model.

| Check | Observed tool result and client explanation |
| --- | --- |
| Robot capabilities | Six declared follower position-command endpoints on `arm_bus`, six leader observation endpoints on `leader_bus`, native ticks relative to device zero. |
| Concrete limits | Joint 1: 973–3034 operational ticks. Joint 2's idle value 847 is below its operational minimum 858; idle/teardown bypass the ordinary position check. |
| Valid candidate | `validate_config` returned `OK`; Codex distinguished configuration integrity from physical readiness. |
| Malformed candidate | `INVALID_ARGUMENT` at load, line 2 column 1; Codex identified a syntax error. |
| Missing board reference | `NOT_FOUND` at integrity; Codex identified actuator `sts3215_servo_1` referring to undeclared `missing_bus`. |
| Missing capabilities | The report marks the Float32 observations unavailable to the current JointState-only observer. Codex explained that leader readings do not prove follower motion and that no MCP motion/task verifier or established pick-and-place capability is available. |
| Concurrent requests | Three initial parallel validation calls received the busy response; sequential retries returned all expected results. |

The run contained seven actual MCP calls: four completed requested inspections
and three busy responses. The client supplied a natural-language explanation;
these were not prewritten SDK responses. Automated SDK tests separately cover
the protocol, all eight registered read-only tools, input restrictions and
recovery after oversized results.

## What this establishes

The selected client can retrieve and explain configured robot contracts and
gaps. Command exposure in this report currently covers Float32 native position
commands on the evidenced STS3215/Feetech path; other command types are reported
as gaps. A future verified movement still needs controlled-joint feedback,
known reference and tolerance, an execution contract and independent operator
confirmation. This inspection does not establish hardware readiness.
