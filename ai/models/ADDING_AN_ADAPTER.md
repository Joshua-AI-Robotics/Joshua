# Adding an inference adapter (step-by-step)

This is the complete, from-scratch guide to plugging a new model into the
Joshua inference pipeline. By the end you will have a working model that
the `inference` node can run, selected purely from a config preset.

If you only want the 30-second version, see the checklist at the
[bottom](#final-checklist).

---

## 1. Mental model

The inference system has two halves:

- **The engine** (`ai/inference/`) — a single, model-agnostic ROS2 node
  (`InferenceHost`). It owns all ROS concerns: subscriptions, publishers,
  QoS, decoding inbound messages, scheduling, denormalizing, and
  publishing. **You never edit this to add a model.**
- **The plug-ins** (`ai/models/<model>/`) — one directory per model,
  containing its config schema (`*.proto`) and its `adapter.py`. Adapters
  are **ROS-free**: they receive decoded `Observation` objects and return
  `ActionCommand` objects.

```
ai/models/<your_model>/        ai/inference/ (engine, untouched)
  <your_model>_config.proto ─▶ ai_model.proto oneof
  adapter.py  ──────────────▶ registry ─▶ InferenceHost
```

Your job is to write **one adapter class** + **one config message** and
**register** it. That is the entire surface area.

---

## 2. The adapter contract

Your adapter subclasses `ai.inference.adapter.InferenceAdapter`. Here is the
full contract — what each method is for, whether it is required, and
exactly when the host calls it.

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

### Base-class attributes you get for free

After `super().__init__(...)`:

| Attribute | Meaning |
| --- | --- |
| `self._single_model` | The `SingleModel` proto for this node (`model_type`, `node`, model config oneof, pretrained paths). |
| `self._config` | The full `Config` proto (robot, perceptions, etc.). |
| `self._num_subscriptions` | `len(node.subscriptions)`. |
| `self._num_publishers` | `len(node.publishers)`. |

To read **your** config message, pull it off `self._single_model`:

```python
self._model_config = single_model.my_model_config  # the oneof field you added
```

---

## 3. The data contracts

Defined in `ai/inference/proto/inference.proto` and `ai/inference/types.py`.

### `Observation` (input to your adapter)

| Field | Type | Notes |
| --- | --- | --- |
| `channel_index` | `int` | Index into `node.subscriptions`. |
| `topic` | `str` | Source ROS topic. |
| `role` | `ChannelRole` | `IMAGE`, `SCALAR`, or `UNKNOWN`. |
| `payload` | `Any` | **Already decoded.** `IMAGE` → numpy array `(H, W, C)` `uint8` rgb8. `SCALAR` → `float`. Never a raw ROS message. |
| `timestamp_ns` | `int` | Host receive time. |

### `ActionCommand` (output from your adapter)

| Field | Type | Notes |
| --- | --- | --- |
| `publisher_index` | `int` | Index into `node.publishers`. |
| `value` | `float` | The command value. |
| `normalized` | `bool` | If `True`, `value` must be in `[-1, 1]`; the host denormalizes it to raw actuator units using the actuator's operational limits (looked up by topic). If `False`, `value` is published as-is. |

### `ChannelRole` enum

`UNKNOWN = 0`, `IMAGE = 1`, `SCALAR = 2`. Derived from each
subscription's `Ros2DataType` (`IMAGE` → `IMAGE`, `FLOAT32` → `SCALAR`,
everything else → `UNKNOWN`).

### `AdapterSpec` (your declared contract)

| Field | Type | Default (proto3) | Notes |
| --- | --- | --- | --- |
| `trigger_mode` | `TriggerMode` | `EVENT` (0) | `EVENT` or `TICK`. |
| `tick_hz` | `float` | `0.0` | Required `> 0` when `TICK`. |
| `min_subscriptions` | `uint32` | `0` | Host fails if node has fewer. |
| `min_publishers` | `uint32` | `0` | Host fails if node has fewer. |

> Proto3 scalar defaults are `0`, so set `min_subscriptions` /
> `min_publishers` explicitly (or use the `make_adapter_spec()` helper in
> `types.py`, which defaults them to `1`).

---

## 4. Host lifecycle (exact call order)

When the `inference` node starts for your model:

```
1. host selects the SingleModel whose node.id matches this node
2. host runs generic node validation (model_type set, >=1 pub, >=1 sub)
3. registry.create_adapter(...) -> YourAdapter.from_config -> __init__
4. adapter.validate()          # your config checks
5. adapter.initialize()        # load weights
6. spec = adapter.spec()
7. host validates spec (min subs/pubs; tick_hz > 0 if TICK)
8. host creates publishers + subscriptions (with QoS from node config)
9. if TICK: host starts a timer at spec.tick_hz

--- running ---
  on each subscription message:
     decode ROS msg -> Observation -> adapter.on_observation(obs)
     -> publish any returned ActionCommands
  every 1/tick_hz seconds (TICK only):
     adapter.on_tick() -> publish any returned ActionCommands

--- shutdown ---
  adapter.close()
```

Exceptions raised in `on_observation` / `on_tick` / publishing are caught
and logged by the host — a single bad frame will not crash the node.

---

## 5. Trigger modes

Choose based on how your model should fire.

### `EVENT` (reactive)

`on_observation` is called for every decoded message and **you** decide
when to emit. Use this when inference is driven by inputs (e.g. run when a
new camera frame arrives; buffer N samples before acting). Both shipped
adapters (`random_noise`, `smolvla`) use `EVENT`.

```python
def spec(self):
    return AdapterSpec(trigger_mode=TriggerMode.EVENT,
                       min_subscriptions=1, min_publishers=1)
```

### `TICK` (fixed-rate)

The host also calls `on_tick()` at `tick_hz`. Use this for steady control
loops where outputs should be published at a fixed cadence regardless of
input timing. Typically `on_observation` just updates internal state and
returns `None`, while `on_tick` emits the commands.

```python
def spec(self):
    return AdapterSpec(trigger_mode=TriggerMode.TICK, tick_hz=50.0,
                       min_subscriptions=1, min_publishers=1)
```

---

## 6. Worked example: a `Smoother` adapter

We will build a complete new model called **Smoother**: it subscribes to N
scalar joint positions, keeps an exponential moving average (EMA) of each,
and republishes the smoothed values at a fixed rate. This exercises
**scalar observations** and **`TICK` mode** end to end.

Assume the i-th subscription corresponds to the i-th publisher.

### Step 1 — create the directory and config proto

Create `ai/models/smoother/smoother_config.proto`:

```protobuf
syntax = "proto3";

package ai.models;

message SmootherConfig {
  // EMA factor in (0, 1]. Higher = follows input faster (less smoothing).
  double alpha = 1;
  // Output publish rate in Hz (drives the TICK timer).
  double tick_hz = 2;
}
```

### Step 2 — write the adapter

Create `ai/models/smoother/adapter.py`:

```python
"""Exponential moving-average smoother adapter.

Subscribes to N scalar inputs, maintains a per-channel EMA, and publishes
the smoothed values at a fixed rate. The i-th subscription maps to the
i-th publisher.
"""

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
    """Publishes an EMA of each scalar input at a fixed tick rate."""

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
        # Update internal state only; publishing happens on the tick.
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
                return None  # wait until every channel has a sample
            values = list(self._ema)
        return [
            ActionCommand(publisher_index=i, value=float(values[i]), normalized=False)
            for i in range(self._num_publishers)
        ]
```

### Step 3 — register in the proto catalog

In `ai/proto/ai_model.proto`:

```protobuf
import "ai/models/smoother/smoother_config.proto";   // add with the others

enum ModelType {
  MODEL_TYPE_INVALID = 0;
  RANDOM_NOISE = 1;
  SMOLVLA = 2;
  SMOOTHER = 3;                                       // new value
}

message SingleModel {
  // ... existing fields 1-4 ...
  oneof model_config {
    RandomNoiseConfig random_noise_config = 5;
    SmolVLAConfig smolvla_config = 6;
    SmootherConfig smoother_config = 7;               // next free field number
  }
}
```

Then add the proto dep in `ai/proto/BUILD` under `ai_model_proto.deps`:

```python
"//ai/models/smoother:smoother_config_proto",
```

### Step 4 — the model `BUILD`

Create `ai/models/smoother/BUILD`:

```python
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:defs.bzl", "py_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

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
    deps = [
        "//ai/proto:ai_model_py_proto",
        "//ai/inference:adapter",
        "//ai/inference:types",
        "//config/proto:config_py_proto",
        requirement("protobuf"),
        # + any model-specific requirement("...") deps (torch, etc.)
    ],
)
```

> Keep heavy dependencies (torch, lerobot, opencv, ...) on **this** target
> only. Because the registry imports adapters lazily, a config that selects
> a different model will never import them.

### Step 5 — wire the registry

In `ai/models/registry.py`:

```python
def _load_smoother():
    from ai.models.smoother.adapter import SmootherAdapter

    return SmootherAdapter


ADAPTER_LOADERS = {
    ai_model_pb2.ModelType.RANDOM_NOISE: _load_random_noise,
    ai_model_pb2.ModelType.SMOLVLA: _load_smolvla,
    ai_model_pb2.ModelType.SMOOTHER: _load_smoother,   # new
}
```

And add the dep in `ai/models/BUILD` under the `registry` target:

```python
"//ai/models/smoother:adapter",
```

### Step 6 — add a config preset

In your `config/config_preset/<robot>/<preset>.pbtxt`, under `ai.models`,
add the inference node. The node `id` must be unique, `node_type` is
`INFERENCE`, and publisher topics must match the actuator subscription
topics you want to drive.

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
bazel build //ai/models/smoother:adapter //ai/models:registry //ros2:inference

# Run the node directly (node_name, node_id, config_path):
source /opt/ros/humble/setup.bash
bazel run //ros2:inference -- inference 5 \
  $PWD/config/config_preset/<robot>/<preset>.pbtxt
```

The host will log the selected adapter, mode, and topics on startup.

---

## 7. Reference details you must respect

### Publishing semantics

- Only **`FLOAT32`** publishers are supported today. The host raises if a
  publisher's `ros2_data_type` is anything else.
- `ActionCommand.normalized == True` requires that the publisher topic
  matches an actuator subscription topic in `robot.actions`, so the host
  can look up `operational_lower_limit` / `operational_upper_limit` and
  denormalize `[-1, 1]` → raw ticks. If no limits are found, the command
  is dropped with an error. Use `normalized=False` to publish raw values
  directly (as the `Smoother` and `random_noise` adapters do).

### Observation decoding

| Subscription `ros2_data_type` | `role` | `payload` type |
| --- | --- | --- |
| `IMAGE` | `ChannelRole.IMAGE` | numpy `(H, W, C)` `uint8`, rgb8 |
| `FLOAT32` | `ChannelRole.SCALAR` | `float` |
| anything else | `ChannelRole.UNKNOWN` | the raw message (avoid) |

### Threading

Subscription callbacks and the tick timer run on rclpy's executor. If your
adapter holds mutable state touched by both `on_observation` and
`on_tick` (or by multiple subscriptions), guard it with a `threading.Lock`
as in the example. Do heavy/model compute **outside** the lock where
possible (copy state under the lock, then run inference).

### Validation strategy

- Put model-specific config checks in `validate()` (raise `ValueError`).
- Declare structural requirements (`min_subscriptions`, `min_publishers`,
  `tick_hz`) in `spec()`; the host enforces them.
- The host already checks the generic invariants (model type set, node id
  set, at least one publisher and one subscription).

---

## 8. Testing your adapter (no ROS required)

Because adapters are ROS-free, you can unit-test them with plain Python by
constructing `Observation` objects and asserting on the returned
`ActionCommand`s. Build a `SingleModel`/`Config` proto in-memory (or parse
a small text proto), instantiate the adapter, and drive it:

```python
import time

from ai.models.smoother.adapter import SmootherAdapter
from ai.inference.types import ChannelRole, Observation

adapter = SmootherAdapter(single_model, config)  # protos built in the test
adapter.validate()
adapter.initialize()

adapter.on_observation(
    Observation(0, "joint_0", ChannelRole.SCALAR, 100.0, time.time_ns())
)
commands = adapter.on_tick()
assert commands[0].publisher_index == 0
```

No `rclpy`, no running ROS graph.

---

## 9. Common pitfalls

- **Loading weights in `__init__`.** Do it in `initialize()` so config
  validation can fail fast before paying the load cost.
- **Forgetting `super().__init__(single_model, config)`.** You lose
  `_num_publishers` / `_num_subscriptions` / `_single_model`.
- **Wrong proto field number.** The `oneof` field number must be unused
  across the whole `SingleModel` message (next free is `7`).
- **`TICK` without `tick_hz`.** The host rejects `tick_hz <= 0` in TICK
  mode.
- **Publishing normalized values to a non-actuator topic.** Limit lookup
  will fail; either publish to the actuator topic or use `normalized=False`.
- **Heavy deps in the wrong `BUILD`.** Keep torch/lerobot/etc. scoped to
  your model's `adapter` target, not shared targets.
- **Mutating shared state without a lock** when using `TICK` + multiple
  subscriptions.

---

## Final checklist

- [ ] `ai/models/<model>/<model>_config.proto` — config message
- [ ] `ai/models/<model>/adapter.py` — subclass of `InferenceAdapter`
      implementing `spec()` and `on_observation()` (+ `validate`,
      `initialize`, `on_tick`, `close` as needed)
- [ ] `ai/models/<model>/BUILD` — proto targets + `adapter` `py_library`
      (heavy deps scoped here)
- [ ] `ai/proto/ai_model.proto` — `import`, new `ModelType` value, new
      `oneof model_config` field
- [ ] `ai/proto/BUILD` — add the new `*_config_proto` to `ai_model_proto`
- [ ] `ai/models/registry.py` — lazy loader + `ADAPTER_LOADERS` entry
- [ ] `ai/models/BUILD` — add `//ai/models/<model>:adapter` to `registry`
- [ ] `config/config_preset/.../*.pbtxt` — an `ai.models.single_models`
      entry selecting your model
- [ ] `bazel build //ros2:inference` passes
- [ ] `./hooks/lint_check.sh --fix` is clean

The engine (`ai/inference/`) is never modified.
