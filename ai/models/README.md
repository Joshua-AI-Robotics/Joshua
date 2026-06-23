# Models (inference plug-ins)

One directory per model: config schema, adapter, and manifest. The engine in
[`ai/inference`](../inference) stays model-free.

For the full **add-a-model workflow** (base + model locks, Bazel hubs),
see [`../README.md`](../README.md). Model venvs always include the global
base; optional `requirements_lock` adds model-specific packages.

## Layout

```text
ai/models/
  registry.py              # manifest-driven; do not edit per model
  BUILD                    # sources + manifests filegroups
  <model>/
    <model>_config.proto   # per-model config message
    adapter.py             # InferenceAdapter subclass
    model.textproto        # entrypoint; optional model-specific lock
    BUILD                  # proto targets + adapter py_library
    requirements.in        # model-only pins (omit if base is enough)
    requirements.lock      # generated model-specific lock
```

| Concern | Location |
| --- | --- |
| Model enum | `ai/proto/ai_model.proto` (`ModelType`, `SingleModel` oneof) |
| Adapter contract | `ai/inference/adapter.py` |
| Manifest schema | `ai/inference/proto/model_manifest.proto` |
| Wiring | `model.textproto` + `sources` / `manifests` in this `BUILD` |

```
ai/models/<your_model>/        ai/inference/ (engine, untouched)
  <your_model>_config.proto ─▶ ai_model.proto oneof
  model.textproto + adapter.py ─▶ registry ─▶ InferenceHost
```

Your job per model: **config proto** + **adapter** + **manifest**. Registry
wiring is automatic from `model.textproto`.

---

## Adapter contract

Subclass `ai.inference.adapter.InferenceAdapter`.

| Method | Required | Called | Purpose |
| --- | --- | --- | --- |
| `__init__(single_model, config)` | inherited | once, at construction | Wire up state. Call `super().__init__(...)`. Do **not** load weights here. |
| `spec() -> AdapterSpec` | **yes** (abstract) | once, after `initialize()` | Declare timing mode + minimum wiring. Validated by the host. |
| `validate() -> None` | optional | once, before `initialize()` | Check model-specific config. Raise `ValueError` on bad config. |
| `initialize() -> None` | optional | once, before serving | Heavy setup: load checkpoints, build processors. |
| `on_observation(observation) -> list[ActionCommand] \| None` | **yes** (abstract) | per decoded input message | Core logic. Return commands to publish now, or `None`. |
| `on_tick() -> list[ActionCommand] \| None` | optional | at `tick_hz`, only if `TriggerMode.TICK` | Fixed-rate control loop hook. |
| `from_config(single_model, config)` | optional classmethod | by the registry | Override only for custom construction. |
| `close() -> None` | optional | once, on shutdown | Release resources. |

### Base-class attributes

After `super().__init__(...)`:

| Attribute | Meaning |
| --- | --- |
| `self._single_model` | The `SingleModel` proto for this node. |
| `self._config` | The full `Config` proto. |
| `self._num_subscriptions` | `len(node.subscriptions)`. |
| `self._num_publishers` | `len(node.publishers)`. |

Read your config from the oneof:

```python
self._model_config = single_model.my_model_config
```

---

## Data contracts

Defined in `ai/inference/proto/inference.proto` and `ai/inference/types.py`.

### `Observation` (input)

| Field | Type | Notes |
| --- | --- | --- |
| `channel_index` | `int` | Index into `node.subscriptions`. |
| `topic` | `str` | Source ROS topic. |
| `role` | `ChannelRole` | `IMAGE`, `SCALAR`, or `UNKNOWN`. |
| `payload` | `Any` | **Already decoded.** `IMAGE` → numpy `(H, W, C)` `uint8` rgb8. `SCALAR` → `float`. |
| `timestamp_ns` | `int` | Host receive time. |

### `ActionCommand` (output)

| Field | Type | Notes |
| --- | --- | --- |
| `publisher_index` | `int` | Index into `node.publishers`. |
| `value` | `float` | The command value. |
| `normalized` | `bool` | If `True`, `value` in `[-1, 1]`; host denormalizes via actuator limits. |

### `ChannelRole`

`UNKNOWN = 0`, `IMAGE = 1`, `SCALAR = 2`. Derived from subscription
`Ros2DataType` (`IMAGE` → `IMAGE`, `FLOAT32` → `SCALAR`).

### `AdapterSpec`

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `trigger_mode` | `TriggerMode` | `EVENT` | `EVENT` or `TICK`. |
| `tick_hz` | `float` | `0.0` | Required `> 0` when `TICK`. |
| `min_subscriptions` | `uint32` | `0` | Host fails if node has fewer. |
| `min_publishers` | `uint32` | `0` | Host fails if node has fewer. |

Use `make_adapter_spec()` in `types.py` to default min subs/pubs to `1`.

---

## Host lifecycle

```
1. host selects SingleModel for this node id
2. generic node validation
3. registry.create_adapter() -> YourAdapter.from_config -> __init__
4. adapter.validate()
5. adapter.initialize()
6. spec = adapter.spec(); host validates spec
7. host creates publishers + subscriptions
8. if TICK: timer at spec.tick_hz

--- running ---
  on each message: decode -> Observation -> on_observation -> publish
  if TICK: on_tick -> publish

--- shutdown ---
  adapter.close()
```

Exceptions in `on_observation` / `on_tick` are logged; one bad frame does not
crash the node.

---

## Trigger modes

### `EVENT` (reactive)

`on_observation` runs per message; you decide when to emit. Used by
`random_noise` and `smolvla`.

```python
def spec(self):
    return AdapterSpec(trigger_mode=TriggerMode.EVENT,
                       min_subscriptions=1, min_publishers=1)
```

### `TICK` (fixed-rate)

Host also calls `on_tick()` at `tick_hz`. Use when outputs must publish on a
fixed cadence. Often `on_observation` updates state and returns `None`.

```python
def spec(self):
    return AdapterSpec(trigger_mode=TriggerMode.TICK, tick_hz=50.0,
                       min_subscriptions=1, min_publishers=1)
```

---

## Worked example: `Smoother` adapter

A scalar EMA smoother using `TICK` mode. Assumes i-th subscription maps to
i-th publisher.

### Step 1 — config proto

`ai/models/smoother/smoother_config.proto`:

```protobuf
syntax = "proto3";

package ai.models;

message SmootherConfig {
  double alpha = 1;    // EMA factor in (0, 1]
  double tick_hz = 2;  // output publish rate
}
```

### Step 2 — adapter

`ai/models/smoother/adapter.py`:

```python
"""Exponential moving-average smoother adapter."""

from __future__ import annotations

import threading
from typing import List, Optional

from ai.proto.ai_model_pb2 import SingleModel

from ai.inference.adapter import InferenceAdapter
from ai.inference.types import (
    ActionCommand,
    AdapterSpec,
    ChannelRole,
    Observation,
    TriggerMode,
)
from config.proto import config_pb2


class SmootherAdapter(InferenceAdapter):
    def __init__(self, single_model: SingleModel, config: config_pb2.Config):
        super().__init__(single_model, config)
        self._model_config = single_model.smoother_config
        self._alpha = self._model_config.alpha
        self._ema: List[Optional[float]] = [None] * self._num_subscriptions
        self._lock = threading.Lock()

    def spec(self) -> AdapterSpec:
        return AdapterSpec(
            trigger_mode=TriggerMode.TICK,
            tick_hz=self._model_config.tick_hz,
            min_subscriptions=1,
            min_publishers=1,
        )

    def validate(self) -> None:
        if not 0.0 < self._model_config.alpha <= 1.0:
            raise ValueError("alpha must be in (0, 1]")
        if self._model_config.tick_hz <= 0.0:
            raise ValueError("tick_hz must be > 0")
        if self._num_subscriptions != self._num_publishers:
            raise ValueError(
                "Smoother needs one publisher per subscription "
                f"({self._num_subscriptions} subs, {self._num_publishers} pubs)"
            )

    def on_observation(self, observation: Observation) -> Optional[List[ActionCommand]]:
        if observation.role != ChannelRole.SCALAR:
            return None
        with self._lock:
            index = observation.channel_index
            x = float(observation.payload)
            prev = self._ema[index]
            self._ema[index] = x if prev is None else (
                self._alpha * x + (1.0 - self._alpha) * prev
            )
        return None

    def on_tick(self) -> Optional[List[ActionCommand]]:
        with self._lock:
            if any(v is None for v in self._ema):
                return None
            values = list(self._ema)
        return [
            ActionCommand(publisher_index=i, value=float(values[i]), normalized=False)
            for i in range(self._num_publishers)
        ]
```

### Step 3 — proto catalog

In `ai/proto/ai_model.proto`:

```protobuf
import "ai/models/smoother/smoother_config.proto";

enum ModelType {
  MODEL_TYPE_INVALID = 0;
  RANDOM_NOISE = 1;
  SMOLVLA = 2;
  SMOOTHER = 3;
}

message SingleModel {
  oneof model_config {
    RandomNoiseConfig random_noise_config = 5;
    SmolVLAConfig smolvla_config = 6;
    SmootherConfig smoother_config = 7;
  }
}
```

Add in `ai/proto/BUILD`: `"//ai/models/smoother:smoother_config_proto"`.

### Step 4 — model `BUILD`

```python
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:defs.bzl", "py_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

exports_files(["model.textproto"])

proto_library(
    name = "smoother_config_proto",
    srcs = ["smoother_config.proto"],
)

py_proto_library(
    name = "smoother_config_py_proto",
    visibility = ["//visibility:public"],
    deps = [":smoother_config_proto"],
)

py_library(
    name = "adapter",
    srcs = ["adapter.py"],
    data = ["model.textproto"],
    deps = [
        "//ai/proto:ai_model_py_proto",
        "//ai/inference:adapter",
        "//ai/inference:types",
        "//config/proto:config_py_proto",
        requirement("protobuf"),
    ],
)
```

Keep heavy deps (torch, lerobot, …) on the model `adapter` target only.

### Step 5 — manifest and filegroups

`ai/models/smoother/model.textproto`:

```text
name: "smoother"
model_type: SMOOTHER
entrypoint: "ai.models.smoother.adapter:SmootherAdapter"
requirements_lock: "ai/models/smoother/requirements.lock"
python_version: "3.10"
```

Add `requirements.in` with model-only deps, run `tools/lock_model.sh smoother`,
and register the lock in `ai/models/BUILD` `locks` filegroup.

In `ai/models/BUILD`, add to `sources` and `manifests`:

```python
"//ai/models/smoother:adapter",
"//ai/models/smoother:model.textproto",
```

Do **not** edit `registry.py`. Register pip hubs in `MODULE.bazel` per
[`../README.md`](../README.md).

### Step 6 — config preset

```text
ai {
  models {
    single_models {
      model_type: SMOOTHER
      node {
        id: 5
        node_type: INFERENCE
        qos_setting {}
        subscriptions {
          ros2_data_type: FLOAT32
          topic: "sts3215_encoder_1_state"
        }
        publishers {
          ros2_data_type: FLOAT32
          topic: "sts_motor_1/position"
        }
      }
      smoother_config {
        alpha: 0.2
        tick_hz: 50.0
      }
    }
  }
}
```

### Step 7 — build and run

```bash
bazel build //ai/models/smoother:adapter //ros2:inference
./hooks/lint_check.sh --fix

source /opt/ros/humble/setup.bash
bazel run //ros2:inference -- inference 5 \
  $PWD/config/config_preset/<robot>/<preset>.pbtxt
```

---

## Reference details

### Publishing

- Only **`FLOAT32`** publishers are supported today.
- `normalized=True` requires the publisher topic to match an actuator
  subscription in `robot.actions` for limit lookup. Use `normalized=False`
  to publish raw values.

### Observation decoding

| `ros2_data_type` | `role` | `payload` |
| --- | --- | --- |
| `IMAGE` | `ChannelRole.IMAGE` | numpy `(H, W, C)` `uint8` rgb8 |
| `FLOAT32` | `ChannelRole.SCALAR` | `float` |
| other | `ChannelRole.UNKNOWN` | raw message (avoid) |

### Threading

`on_observation` and `on_tick` run on the rclpy executor. Guard shared state
with a `threading.Lock` when using `TICK` with multiple subscriptions.

### Validation

- Model-specific checks → `validate()`.
- Structural requirements → `spec()` (host enforces).

---

## Testing without ROS

```python
import time

from ai.models.smoother.adapter import SmootherAdapter
from ai.inference.types import ChannelRole, Observation

adapter = SmootherAdapter(single_model, config)
adapter.validate()
adapter.initialize()

adapter.on_observation(
    Observation(0, "joint_0", ChannelRole.SCALAR, 100.0, time.time_ns())
)
commands = adapter.on_tick()
assert commands[0].publisher_index == 0
```

---

## Common pitfalls

- Loading weights in `__init__` — use `initialize()`.
- Forgetting `super().__init__(single_model, config)`.
- Wrong proto `oneof` field number on `SingleModel`.
- `TICK` without `tick_hz > 0`.
- `normalized=True` on a non-actuator topic.
- Heavy deps on shared targets instead of the model `adapter` target.
- Mutable state without a lock under `TICK` + multiple subscriptions.

---

## Checklist

- [ ] `ai/models/<model>/<model>_config.proto`
- [ ] `ai/models/<model>/adapter.py` (`spec()`, `on_observation()` minimum)
- [ ] `ai/models/<model>/model.textproto` (`entrypoint`; optional `requirements_lock`)
- [ ] `ai/models/<model>/requirements.in` + lock (only when deps exceed the global base)
- [ ] `ai/proto/ai_model.proto` + `ai/proto/BUILD`
- [ ] `ai/models/BUILD` — `sources`, `manifests`, `locks`
- [ ] Config preset `single_models` entry
- [ ] `MODULE.bazel` — `joshua_pip_<model>` hubs (only when the model has a lock)
- [ ] `bazel build //ros2:inference` and lint clean

The engine (`ai/inference/`) is never modified.
