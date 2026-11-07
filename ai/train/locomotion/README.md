# Spider locomotion training

This isolated Python 3.12 environment brings the [twelve-motor STS3215 spider](../../../tools/cad/SPIDER_STS3215.md)
into mjlab for batched MuJoCo physics and PPO. The first task holds a standing
pose at zero commanded velocity; a second task trains low-speed forward walking.
It follows MicroDuck's progression from a
verified robot model to batched RL, policy export, CPU replay, and hardware
qualification. Joshua inference integration is a later step;
the [Joshua simulation backend](../../../simulation/README.md) currently
provides viewing/playback rather than this trainer.

This Compose project has no robot-device mounts or ROS launcher. Only setup
uses networking; preparation, checks, and smoke runs have networking disabled.
Its model is a simulation prototype with estimated mass and unmeasured servo
dynamics. No physical robot has been qualified by these checks.

## Quick start

Use an x86_64 Ubuntu host with Docker Compose v2, the Joshua Ubuntu 24 image,
and the [CAD setup](../../../tools/cad/README.md). Generate the spider exports
first; each generation command refuses to overwrite an existing CAD document.

```bash
docker compose build joshua-u24
# If the spider output does not exist yet:
./scripts/run_robotcad.sh --robot spider --create
./scripts/run_robotcad.sh --robot spider --verify-exports
./scripts/run_robotcad.sh --robot spider --convert-mujoco

./scripts/run_spider_rl.sh setup
./scripts/run_spider_rl.sh prepare
./scripts/run_spider_rl.sh check
./scripts/run_spider_rl.sh cpu-smoke
./scripts/run_spider_rl.sh smoke
```

`setup` installs the hashed Linux/Python 3.12 lock into an output-local venv.
It uses mjlab 1.3.0, MuJoCo/MuJoCo-Warp 3.8.1, Warp 1.12.0, and PyTorch 2.9.1.
These are separate from the CAD Python 3.11/MuJoCo 3.5 environment and Joshua's
Bazel dependencies. `JOSHUA_RL_IMAGE` can select a compatible Ubuntu 24 image.
Run the wrapper from any directory; it selects the repository root and your UID.

`check` runs CPU physics and interface tests. `cpu-smoke` explicitly uses four
CPU environments to test the manager/PPO path. `smoke` requires NVIDIA Container
Toolkit configured for Docker and defaults to 64 GPU environments. It selects
the `nvidia` runtime explicitly and exposes GPUs using `NVIDIA_VISIBLE_DEVICES`.
Verify it with:

```bash
docker run --rm --network none --runtime nvidia \
  -e NVIDIA_VISIBLE_DEVICES=all -e NVIDIA_DRIVER_CAPABILITIES=compute,utility \
  --entrypoint nvidia-smi joshua:u24-jazzy
```

See the [host setup guide](../../../docs/GETTING_STARTED.md) for installation.
After installing the toolkit and reloading Docker, explicit runtime selection
can work while `--gpus all` still reports `could not select device driver ...
capabilities: [[gpu]]`. The explicit-runtime path avoids restarting Docker just
to refresh GPU device discovery. The CPU smoke does not establish GPU
compatibility or throughput. No automatic CPU fallback occurs.

Each smoke holds a zero target for three seconds, checks action saturation and
reset feedback, then runs five PPO iterations with 24 policy steps per update.
It saves checkpoints, normalization state, TensorBoard events, a contract, and
`smoke.json` in a new `output/spider_rl/runs/<UTC timestamp>/` directory.
Five iterations check the pipeline; they do not produce a walking controller.
Logging stays local and no W&B credentials are required.

## Train and evaluate walking

The walking task reuses the generated asset and twelve actions. Its versioned
`Joshua-Spider-Crawl-v1` contract adds a two-value gait clock to the standing
interface: 47 actor inputs and 50 critic inputs. The standing task stays at
45/48. Benchmark both batch sizes before starting a bounded run:

```bash
./scripts/run_spider_rl.sh train --num-envs 512 --iterations 10 --benchmark
./scripts/run_spider_rl.sh train --num-envs 1024 --iterations 10 --benchmark
./scripts/run_spider_rl.sh train --num-envs 1024 --iterations 2000
```

Each command prints `RUN_DIR`. Benchmark outputs go under `output/spider_rl/benchmarks/`;
training outputs go under `output/spider_rl/walk/`. `training.json` records batch
size, updates, policy-transition throughput, versions, seed, and final checkpoint.
This throughput counts policy transitions (each contains 20 physics substeps).
It includes first-update overhead and is a short-run benchmark, not a sustained
training guarantee. Reported Torch peak allocation excludes Warp/driver allocations.
Each run also saves the policy contract, full environment YAML, PPO configuration,
TensorBoard events, source snapshots, and checkpoints every 100 updates.

Commands start at zero, change to 0.02–0.03 m/s after 1,200 policy steps
(50 updates), then to 0.03–0.05 m/s after 6,000 steps (250 updates). The standing
fraction falls from 100% to 20%, then 10%. After 24,000 steps (1,000 updates),
the moving cases use exactly 0.05 m/s. Resampling applies changes gradually
at the next command/reset; stages use the persisted global policy-step counter,
not the number of parallel environments. Lateral and turning commands remain
zero in this first forward-walking task. Resets vary joint angles by ±0.01 rad,
roll/pitch by ±0.02 rad, and yaw by ±0.05 rad.

Planar velocity tracking uses a 0.03 m/s tolerance: standing at a 0.05 m/s command
does not receive a near-perfect tracking reward. Vertical velocity has a separate
penalty so foot lift does not remove correct horizontal tracking credit.
An additional unbounded squared planar-error cost prevents a policy from earning
credit by slowly rocking forward and rapidly snapping backward with no net progress.
An unbounded yaw-rate cost rejects circling while matching body-frame speed;
a raw-output bounds cost keeps predictions near the usable action interval.
Other terms encourage upright support, a 12 mm swing-foot clearance gated by
actual body progress, and home posture; they penalize stance
slip, fewer than two supporting feet, height deviations, torque, mechanical
power, joint overspeed, and abrupt actions. The 2 rad/s overspeed term is a soft
training penalty. It does not turn the provisional servo into an identified
motor model or enforce a hard physical speed cap.

The actor receives `sin(2π phase), cos(2π phase)` after its original 45 values.
Phase is the episode policy-step count times 0.02 s, divided by a 0.8-second
period, modulo one; while commanded to stand it is zero (`[0,1]`). A contact
reward guides a FL → RR → FR → RL crawl with 75% stance duty, one scheduled
swing leg at a time. A swing-lift cost guides its hip/knee control toward a
sine-shaped lift (maximum −0.2/−0.1 rad offsets), leaving stance and yaw control
free. The policy learns the resulting joint motion and forward placement. The clock is
available to a future runtime from policy ticks, without contact ground truth
or a new physical sensor. A future replay/adapter must preserve this clock and
reset semantics. Earlier 45-input walking experiment checkpoints are rejected
by the new versioned contract rather than silently loaded.

PPO sends raw predictions to the reward/action manager. The lift cost sees
out-of-range predictions so saturation cannot hide their error; the bounded
action term still clips physical targets to ±0.2 rad and feeds back clipped
normalized actions. Evaluation records action-saturation fraction as well as
motion metrics.

Resume a local checkpoint with an additional bounded update count:

```bash
./scripts/run_spider_rl.sh train --num-envs 1024 --iterations 500 \
  --resume /workspace/output/spider_rl/walk/<run>/model_1999.pt
```

Paths passed to these commands are **container paths**, rooted at `/workspace`.
Resume checks the contract and restores model/normalization, optimizer, iteration,
and curriculum counter. Physics episodes are reset; RNG state and partial
rollouts are not restored, so continuation is not bit-for-bit identical.

Evaluate a chosen checkpoint independently of its training reward:

```bash
./scripts/run_spider_rl.sh evaluate \
  --checkpoint /workspace/output/spider_rl/walk/<run>/model_1999.pt \
  --episodes 100 --seconds 30 --speed 0.05 --seed-base 10000 --video
```

Each of the 100 cases receives a separately seeded initial pose (seeds
10,000–10,099), a fixed 0.05 m/s forward command, and a 30-second horizon. A
case fails permanently on its first termination/reset, even if its reset robot
later stands. Walking success additionally requires planar velocity RMSE ≤0.02 m/s
after a two-second warm-up and ≥60% of the requested forward distance over that
remaining horizon (0.84 m for the default test). Surviving while stationary
cannot pass. At least 90% of cases must pass all criteria for `gate_passed: true`.
Use `--require-pass` to return a failing exit status when that gate is missed.
The qualification flag also requires at least 100 cases, at least 30 seconds,
exactly the 0.05 m/s target, and deterministic inference. Shorter/easier tests
still report case metrics but cannot pass the full gate.

Evaluation saves `evaluation.json` with per-case first failure, progress, speed
error, heading drift, mean body velocity, stance slip, peak clearance, joint speed,
effort, and checkpoint hash.
Failed cases retain the metrics up to failure and never count as successful.
Stance slip uses foot-site XY speed during contact; with spherical toes this
proxy includes rolling motion. Peak effort and speed are sampled at policy rate.
The video shows up to ten seconds of the first seeded case while it remains
alive; `preview.png` is captured at one second. The mesh-free contact primitives
are made visible in the CPU render model without changing GPU physics.
Reports and MP4/PNG files stay beside the checkpoint in ignored `evaluations/`.
Use held-out seeds (for example `--seed-base 20000`) when comparing final policies.
`--sample-actions` is available for diagnosing dependence on exploration noise;
those reports are labelled `sampled_diagnostic` and cannot pass the deterministic
qualification gate, even if their individual case metrics meet its thresholds.

Passing this nominal-model test establishes a simulation baseline only. Actuator
calibration, domain randomization, normalized ONNX/CPU replay, and Joshua
integration remain subsequent review stages.

## Model and policy interface

Preparation reads the CAD-generated `design.json`, `manifest.json`, and floating
`mujoco/robot.xml`. It preserves all body frames, joint axes/limits, explicit
inertia tensors, and estimated masses. It removes meshes and adds a chassis box,
an upper-body box, leg capsules, and four foot spheres. Every link can contact
the ground. Robot self-collision is disabled because adjacent primitive envelopes
overlap; full enclosure geometry and self-collision require later validation.
These contact proxies are approximate, not a mechanical clearance certificate.
Robot contacts use a 5 ms solref time constant (with priority over the floor)
to keep impact penetration within the 2 mm foot/shin clearance in this model.

The generated standing `policy_contract.json` records asset/source hashes, joint order,
units, limits, gains, and timing. Robot dimensions and drive assumptions come
from CAD; training settings live in the task. This is an offline trainer, not a
second Joshua robot preset. A later Joshua adapter will translate the policy
contract into the protobuf-configured runtime.

| Interface | Definition |
| --- | --- |
| Joint order | `fl`, `fr`, `rl`, `rr`; each `hip_yaw`, `hip_pitch`, `knee_pitch` |
| Home | 12 zeros; the CAD origins already include the physical standing angles |
| Actor, 45 values | body gyro 3, projected gravity 3, joint displacement 12, joint velocity 12, previous action 12, twist command 3 |
| Critic, 48 values | actor plus body linear velocity 3; privileged simulation input |
| Action | 12 normalized values, clipped to ±1, multiplied by 0.2 rad around home, then joint limits |
| Previous action | accepted clipped normalized action, reset to zero each episode |
| Timing | policy 50 Hz, physics 1 kHz, 20 substeps per policy action |
| Frames | body x forward, y left, z up; MJCF quaternion wxyz |
| Normalization | separate actor/critic running statistics in the saved PPO checkpoint |

The actor receives no base linear velocity or contact ground truth. Walking run
contracts extend the table above with their explicit gait-clock definition.
Hardware will
need a body IMU and a documented attitude estimator for projected gravity,
timestamped joint state, and matching order/units. There is no observation noise,
delay, push, or mass randomization in this first task. The CAD drive's 0.65 Nm
effort cap is enforced in simulation. Its 2 rad/s speed figure is metadata and
is **not** a hard velocity clamp or an identified STS3215 torque-speed curve.
The position servo has no calibrated backlash, thermal, voltage, or delay model.

The task rewards zero linear/angular velocity, upright posture, and home pose;
it penalizes action changes and joint-limit excursions. Episodes reset on a
40° tilt, root height below 40 mm, non-foot ground contact, or ten-second timeout.
Default resets are deterministic. The action envelope is deliberately narrow.
The standing task is not a gait reward and should not be extended into a long
walking run without reviewing foot lift, slip, energy, and contact behavior.

## Contents

- `mjlab_joshua/prepare.py` — derive the mesh-free MJCF and policy contract.
- `mjlab_joshua/contract.py` — versioned observation/action ordering and units.
- `mjlab_joshua/task.py` — bounded action term, standing task, and PPO settings.
- `mjlab_joshua/smoke.py` — held-target and five-update PPO gates with local evidence.
- `mjlab_joshua/locomotion.py`, `train.py`, and `evaluate.py` — walking rewards,
  speed curriculum, benchmark/resume entrypoints, seeded metrics, and video.
- `tests/` — CAD/training frame/inertia parity, standing/contact physics,
  contract provenance, and posture penalty checks.
- `requirements.in`, `requirements.lock` — isolated direct dependencies and
  hashed transitive pins for Linux x86_64/Python 3.12.
- `container.sh`, [Compose file](../../../docker-compose.spider-rl.yml), and
  [host wrapper](../../../scripts/run_spider_rl.sh) — Docker entrypoints.

All venvs, caches, generated XML/JSON, test evidence, checkpoints, and future
videos remain in Git-ignored `output/spider_rl/`. CAD source files stay in
`tools/cad/`; training consumes generated CAD outputs. Regenerate CAD and then
the training asset after design changes. `check` rejects stale source hashes or
a recipe differing from the generated design snapshot.

To update the lock, run uv 0.9.28 **inside the Ubuntu 24 container** with
`uv pip compile --python-version 3.12 --python-platform x86_64-manylinux_2_35
--generate-hashes requirements.in -o requirements.lock`, then rerun setup and
both smokes. Keep simulator versions paired and leave root/CAD locks separate.
The preparation/check services need no GPU. These optional checks are separate
from the repository's Bazel/Compose CI suites.

## Workstream and review stack

| Stage | Review scope and exit evidence |
| --- | --- |
| CAD parent | Reproducible printable parts, URDF, mesh scale/frames, MuJoCo preview |
| Foundation (#147) | Isolated stack, primitive model, policy contract, standing and PPO smoke |
| Locomotion | Low-speed forward commands/curriculum, gait rewards, seeded evaluations and videos; benchmark 512–1024 envs before scaling |
| Robustness and export | Measured actuator fit, controlled domain randomization, ONNX with normalization, CPU MuJoCo replay parity |
| Joshua integration | Config-selected inference adapter, 50 Hz contract and joint commands, simulation evaluation before hardware |

Start locomotion evaluation with 0.05 m/s commands; the proposed gate is 90 of
100 seeded 30-second episodes without falling, with measured tracking and slip.
Treat that as a target to implement, not a result from this PR. After the parent
merges, retarget/rebase its child onto `develop` before merging the child.

## Responsibilities

- Preserve a reproducible simulation training environment and policy contract.
- Catch model/interface drift before optimization and retain run evidence locally.
- Separate simulation evidence from actuator identification and hardware readiness.

## Non-Goals

- Editing printable CAD, firmware, or physical motor control.
- ONNX export or a Joshua ROS policy adapter in this stage.
- Reusing MicroDuck's XL330 actuator model for STS3215 motors.

## References

- [Pollen Robotics MicroDuck RL](https://github.com/pollen-robotics/microduck_rl):
  mjlab/PPO training, actuator identification, export, and runtime rehearsal.
- [mjlab 1.3.0](https://github.com/mujocolab/mjlab/tree/v1.3.0):
  manager-based environments, MuJoCo-Warp physics, and RSL-RL integration.
