# Inference engine

Follow the repository [instructions](../../AGENTS.md) and applicable ancestor
`AGENTS.md` files before changing or running this subsystem.

Model-agnostic ROS2 inference host and launcher. Concrete models live in
[`ai/models/<model>/`](../models); this package does not import them directly.

**Adding a model:** [`../AGENTS.md`](../AGENTS.md) (workflow and checklists).
**Adapter API and worked example:** [`../models/AGENTS.md`](../models/AGENTS.md).

## Architecture

```
ai/models/<model>/        ai/inference/  (this package)
  model.textproto ─────▶  manifest.load_manifest()
  adapter.py      ─────▶  registry ─▶ InferenceHost

ROS2 topics ─▶ InferenceHost (rclpy Node)
                 │  pub/sub, QoS, decode → Observation
                 ▼
              InferenceAdapter (ROS-free, in ai/models/<model>/)
                 ▼
              ActionCommand(s) ─▶ host publishes to ROS
```

## Key files

| File | Role |
| --- | --- |
| `inference_launcher.py` | Read config → manifest → build venv → re-exec |
| `environment_manager.py` | Sync global base + optional model lock into venv |
| `host_main.py` | Post-re-exec entry: ROS paths + `InferenceHost` |
| `host.py` | `InferenceHost` node |
| `manifest.py` | Load `model.textproto`, resolve workspace paths |
| `adapter.py` | `InferenceAdapter` base class |
| `types.py` | `Observation`, `ActionCommand`; re-exports proto enums |
| `observation_codec.py` | ROS message → canonical payloads |
| `proto/inference.proto` | `ChannelRole`, `TriggerMode`, `AdapterSpec` |
| `proto/model_manifest.proto` | `ModelManifest` (entrypoint, optional lock path) |

## Trigger modes

- `EVENT` — `on_observation` per decoded message; adapter buffers internally.
- `TICK` — host also calls `on_tick` at `AdapterSpec.tick_hz`.

## Motor-policy replay

Replay checks how a [configured policy](proto/motor_policy.proto) maps a known
answer to a joint command or a reason to send no command. The
[rotary-gripper test fixture](testdata/rotary_gripper.pbtxt) supplies the mapping.
The test collects commands in memory.

Run the replay and configuration checks in a Docker dev shell.

```bash
bazel test //ai/inference:motor_policy_test //config:validation_test
```

## Asynchronous motor-policy sessions

A session runs on one asyncio loop with one active evaluation and one replaceable
pending request. A new trigger sample may wait while the previous result is still
admissible. Bind each logical input to a source and sample generation at explicit
start. Stop before changing bindings. Start returns a new request generation.
Use a distinct run identity when creating a new session. Supply the identity of
the validated config used to create its adapter.

Stamp each sample at original host receipt with `time.monotonic_ns()`. This
local clock bounds elapsed network wait and queue time even if RobotTime pauses
or jumps. It measures receipt age only. Source/acquisition timestamps retain
their own domain. Submit, stop, recording and command admission share the owning
loop. Admitted results carry their local monotonic expiry through the sink call.
The record and sink callbacks must be synchronous and nonblocking. An external
source should marshal its updates onto that loop. The control owner handles ROS
hardware binding and physical stop.

The [Decisions adapter test](../models/decisions/tests/adapter_test.py) shows
registry construction, an explicit session start, request submission and result
recording. Run the lifecycle and adapter tests in a Docker dev shell.

```bash
bazel test //ai/inference:policy_session_test //ai/models/decisions:adapter_test
```
