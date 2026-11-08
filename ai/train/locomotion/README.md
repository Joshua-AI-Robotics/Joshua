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
./scripts/run_spider_rl.sh train --num-envs 1024 --iterations 501 --seed 42
```

Each command prints `RUN_DIR`. Benchmark outputs go under `output/spider_rl/benchmarks/`;
training outputs go under `output/spider_rl/walk/`. `training.json` records batch
size, updates, policy-transition throughput, versions, seed, and final checkpoint.
This throughput counts policy transitions (each contains 20 physics substeps).
It includes first-update overhead and is a short-run benchmark, not a sustained
training guarantee. Reported Torch peak allocation excludes Warp/driver allocations.
Each run also saves the policy contract, full environment YAML, PPO configuration,
TensorBoard events, source snapshots, and checkpoints every 100 updates.

To watch robots while PPO is learning, enable the live state stream:

```bash
./scripts/run_spider_rl.sh train --num-envs 1024 --iterations 501 --seed 42 \
  --live --live-envs 16
# In another terminal, using the printed RUN_DIR after live.npz appears:
./scripts/run_spider_rl.sh watch --detach \
  --run /workspace/output/spider_rl/walk/<run>
```

The interactive MuJoCo window shows 16 actual training environments in a CAD
mesh grid while all 1,024 contribute to PPO. The overlay reports the iteration,
policy steps, frame age, and training status. **G** switches grid/close-up,
**Tab** selects another robot in close-up, and **Space** freezes the display.
Freezing or closing the viewer leaves training running. Resets and exploration
noise are visible because these are live training states. PPO optimization may
briefly hold the last pose; completed/interrupted runs are labelled explicitly.

On a host without an active desktop monitor, capture the live training grid
using offscreen rendering:

```bash
./scripts/run_spider_rl.sh watch-video \
  --run /workspace/output/spider_rl/walk/<run> \
  --video /workspace/output/spider_rl/training-scene.mp4 --seconds 10
```

The labelled video samples actual training states in wall-clock time at up to
10 fps. It is not a real-time, deterministic policy rollout. Its accompanying
JSON records each frame's iteration, stream timestamp, and training status.

The trainer publishes only the selected poses, up to 15 times per wall-clock
second (`--live-fps`), through an atomic local `live.npz` file. A separate CPU
viewer reads it, arranges independent environment origins into a display grid,
and renders the CAD meshes. It does not send controls back to training. State
copies add some overhead; use headless runs for throughput comparisons. Both
processes run in Docker without network or robot-device access. The viewer
requires the same X11/XWayland setup as the CAD preview below.

The 2026-11-07 local baseline used the final reward recipe from scratch, seed 42,
and selected `model_500.pt` (501 updates). It passed 100/100 validation cases and
100/100 fresh held-out cases under the full protocol below, with mean speed
RMSE about 0.0102 m/s and mean forward travel about 1.35 m. This is a nominal
simulation result; rerun evaluation after any model, dependency, or reward change.
The default training budget remains 2,000 updates when `--iterations` is omitted.

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
  --resume /workspace/output/spider_rl/walk/<run>/model_500.pt
```

Paths passed to these commands are **container paths**, rooted at `/workspace`.
Resume checks the contract and restores model/normalization, optimizer, iteration,
and curriculum counter. Physics episodes are reset; RNG state and partial
rollouts are not restored, so continuation is not bit-for-bit identical.

Evaluate a chosen checkpoint independently of its training reward:

```bash
./scripts/run_spider_rl.sh evaluate \
  --checkpoint /workspace/output/spider_rl/walk/<run>/model_500.pt \
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

## Train a measured-foot crawl

The v1 baseline can move forward while dragging a foot. The experimental v4
recipe below builds on v2/v3; the version differences are described here so
earlier checkpoints remain reproducible. The `Joshua-Spider-Crawl-v2` objective
replaces v1's hand-picked swing joint angles
with a CAD-derived foot path: 10 mm swing clearance, forward placement, and
backward body-relative stance motion. A Hermite swing joins the stance velocity
at lift-off and touchdown; vertical velocity is zero at both ends. Numerical IK
checks the reference within the existing ±0.2 rad action envelope at 80 phases
and three speeds. `gait_workspace.json` records reachability error and reference
joint speed. This is a reward guide; PPO still supplies all twelve joint targets.

```bash
./scripts/run_spider_rl.sh train --gait-version 4 \
  --num-envs 1024 --iterations 1500 --seed 42 --live --live-envs 16
./scripts/run_spider_rl.sh watch --detach \
  --run /workspace/output/spider_rl/walk/<v4-run>
```

V2 rewards swing lift together with stance support; all-ground shuffling earns
no moving gait-support reward. It measures the foot path and clearance directly,
uses velocity at the spherical foot's ground point for the slip penalty, and
derives neutral body height from CAD. Non-timeout terminations incur a penalty
so ending an episode cannot cheaply avoid motion costs. Motor effort, action
limits, observations, and physics stay as in v1. The 2 rad/s reference remains
a soft penalty; the 0.05 m/s IK trajectory itself reaches about 4.58 rad/s.
This still needs measured actuator dynamics before transfer.

Evaluation and CAD preview select the task version from the checkpoint contract.
V1 is retained for baseline comparisons and remains the training CLI default;
resume rejects a contract/version mismatch. Use the new per-leg checks when
assessing gait quality:

```bash
./scripts/run_spider_rl.sh evaluate \
  --checkpoint /workspace/output/spider_rl/walk/<v4-run>/model_<iteration>.pt \
  --episodes 100 --seconds 30 --seed-base 10000 --require-gait-pass --video
```

`gait_gate_passed` requires the original walking criteria plus, for every leg,
at least 50% airborne time during its scheduled swing and 5 mm mean clearance
during the middle 40% of swing. At least three feet must support the body for
80% of evaluated time. At least 90/100 cases must meet all these criteria in
the full 30-second deterministic protocol. Reports retain each leg's values,
three-foot support, and all-feet-grounded fractions. The original `gate_passed`
field still describes only the earlier walking protocol; the new field is an
additional requirement, not evidence inferred from the training reward.

`--gait-version 3` widens the policy target envelope to ±0.3 rad (±17.2°),
within the CAD limits of ±30° yaw, ±25° hip, and ±30° knee. It uses a one-second
cycle, 12 mm lift, stronger clearance/path guidance, and lower initial exploration
noise. At 0.05 m/s its stance stroke is 37.5 mm. The action scale and clock period
are versioned in the checkpoint contract; old checkpoints retain their original
interpretation. The larger envelope is a simulation experiment, not a measured
hardware travel allowance. Verify simultaneous CAD poses and actual cable/horn
clearances before using a wider range on an assembled robot.

Reproduce the v3 CAD pose check (40 gait phases, 32 single-leg corner poses,
and the macro's neutral assembly check):

```bash
./scripts/run_spider_rl.sh gait-motion --gait-version 3 \
  --output /workspace/output/robotcad/spider_sts3215/gait-v3-motion.json
JOSHUA_CAD_MOTION_FILE=/workspace/output/robotcad/spider_sts3215/gait-v3-motion.json \
  ./scripts/run_robotcad.sh --robot spider --check-cad
```

The initial 73 samples had no unintended solid overlap. The intentional TPU
toe interference is excluded. This sampled result does not cover every
simultaneous combination of all twelve joint targets.

`--gait-version 4` keeps v3's ±0.3 rad envelope, one-second cycle, support
objective, and servo parameters, but compares the IK guide with **measured joint
positions**, instead of raw policy commands. This lets the policy offset its
targets to compensate for the simulated position servo's deflection under load.
An open-loop v3 IK diagnostic showed roughly 0.10 rad mean absolute hip error and
less than 2.2 mm peak toe clearance despite a 12 mm geometric lift target.
Matching target angles alone therefore did not establish actual foot tracking.
V2 and v3 remain available as comparisons with their original reward semantics;
v4 starts a separate run and cannot silently resume a different version.
These are experimental objectives; use the independent gait gate above before
claiming an improved walking policy. The previously selected v1 baseline is
preserved until a candidate has passed evaluation.

The initial v4 run used 1,024 environments, seed 42, and 1,500 PPO updates.
Its final checkpoint passed 100/100 forward-walking cases at 0.05 m/s for
30 seconds (seeds 10,000–10,099), but **0/100 passed the gait gate**: the feet
still largely stayed on the floor. It was not selected over v1. This result
shows why widening the action range and passing the velocity test are
insufficient evidence of a better crawl. The larger-range recipe remains
experimental; the next learning iteration needs to address foot-lift exploration
and the incentive to shuffle, with the same independent per-leg measurements.

## Research-informed hybrid crawl (v5)

The following projects informed the next experiment. Their results and hardware
parameters are not evidence that our robot will behave the same way.

| Reference | Mechanism inspected | Application here |
| --- | --- | --- |
| [Freenove quadruped](https://github.com/Freenove/Freenove_Quadruped_Robot_Kit/tree/master/ArduinoLibraries), `FNQRBasic.cpp` in `FNQR.zip` | Its crawl sequences lift, forward travel, and lowering of individual feet, interleaved with body motion. | Make the stepping sequence explicit; measure every leg rather than accepting forward travel alone. |
| [OpenQuadruped](https://github.com/adham-elarabawy/open-quadruped) | Foot trajectories, three-joint leg IK, and body-pose control are separate components. Its README lists calibration/RL work as unfinished. | Reuse the design principle with our own CAD kinematics, without treating this project as a validated RL baseline. |
| [SpiderPi hybrid](https://github.com/chenpeiting/spider) | The repository contains IK/CPG and joint-oscillator controllers; hybrid action code adds learned corrections to a periodic prior. | Train corrections around a crawl prior. Its six-leg tripod timing and LX-224 servo parameters are not copied. |
| [PMTG](https://arxiv.org/abs/1910.02812) and [AllGaits](https://arxiv.org/abs/2411.04787) | Policies work with trajectory generators/oscillators, with phase available in the observations. | Use an explicit rhythmic prior and retain phase in the policy interface. |

Our four-legged robot uses a one-leg-at-a-time crawl; an eight-legged spider's
contact pattern cannot be transferred directly. V5 is a hybrid controller:
CAD-derived joint trajectories and outer position feedback provide the nominal
motion, while PPO learns twelve bounded position corrections for balance and
velocity tracking. The previous gait versions remain available for comparison.

The nominal foot path has a one-second period and 25 mm geometric swing lift.
Outer position-error gain is 2, phase lead is 40 ms, and velocity feedforward
uses the existing servo's `kv/kp` ratio. These are controller settings, not
changes to the simulated motor's stiffness, damping, or 0.65 Nm torque cap.
The policy adds at most ±0.08 rad per joint; the combined position target is
clipped to ±0.4 rad, within the CAD joint limits. The measured clearance objective
remains 12 mm to account for the difference between nominal commands and loaded
motion. The nominal path alone is not proof of a successful crawl.

V5 checkpoints use a separate contract describing the trajectory, feedback,
phase lead, residual scale, and final target limits. Previous-action observations
contain the accepted normalized residual after final clipping. The same 47 actor
inputs and 50 critic inputs are retained, but a v4 checkpoint cannot be silently
loaded as v5. `target_saturation_fraction` reports clipping of the complete
controller, separately from clipping of the policy residual.

```bash
./scripts/run_spider_rl.sh gait-motion --gait-version 5 \
  --output /workspace/output/robotcad/spider_sts3215/gait-v5-motion.json
JOSHUA_CAD_MOTION_FILE=/workspace/output/robotcad/spider_sts3215/gait-v5-motion.json \
  ./scripts/run_robotcad.sh --robot spider --check-cad
./scripts/run_spider_rl.sh train --gait-version 5 \
  --num-envs 1024 --iterations 1500 --seed 42 --live --live-envs 16
./scripts/run_spider_rl.sh evaluate \
  --checkpoint /workspace/output/spider_rl/walk/<v5-run>/model_1499.pt \
  --episodes 100 --seconds 30 --seed-base 10000 --require-gait-pass
# Ablation: identical physics and test cases, but disable the learned correction.
./scripts/run_spider_rl.sh evaluate \
  --checkpoint /workspace/output/spider_rl/walk/<v5-run>/model_1499.pt \
  --episodes 100 --seconds 30 --seed-base 10000 --zero-residual
```

The ablation is labelled `generator_only`; it is not a learned-policy result.
The forward-walking and per-leg gait gates are unchanged. Validate a candidate
on fresh held-out seeds before selecting it over the existing baseline.

The first v5 run (`20261107T151800313919Z`) trained from scratch with 1,024
environments and seed 42. Checkpoint `model_500.pt` (501 updates, 12.31 million
transitions) passed both gates in **100/100 validation cases and 100/100 held-out
cases**, each lasting 30 seconds at 0.05 m/s. The two sets use initial-pose seeds
10,000–10,099 and 20,000–20,099. Training was stopped after qualification; later
checkpoints in that run are not the selected policy.

| Validation measurement | Previous v4, update 1499 | Selected v5, update 500 |
| --- | --- | --- |
| Forward-walking successes | 100/100 | 100/100 |
| Per-leg gait successes | 0/100 | 100/100 |
| Mean mid-swing clearance, FL/FR/RL/RR | Approximately −0.4 mm each (contact penetration) | 7.1 / 7.5 / 6.1 / 6.1 mm |
| Time with at least three feet supporting | 100% (largely all feet grounded) | 97.6% |
| Planar velocity RMSE | 0.0136 m/s | 0.0161 m/s |

The improved stepping comes with slightly greater velocity variation. V5's
mean forward travel was 1.44 m over the evaluated 28 seconds. Its measured
mid-swing lift is smaller than the nominal 25 mm path, so tracking is still
imperfect. Disabling the learned residual on the same controller/physics left
100/100 robots upright but produced **0/100 walking and gait successes**; the
trajectory prior alone did not meet the speed and per-leg criteria.

The ignored `output/spider_rl/selected_baseline.json` now selects v5 and records
the contract, source hashes, checkpoint hash, and both evaluation reports.
`selected_baseline_v1.json` preserves the previous selection. The corresponding
videos are `output/spider_rl/hybrid-v5-cad-walking.mp4` (the selected policy with
71 CAD meshes) and `output/spider_rl/hybrid-v5-training-scene.mp4` (16 actual
stochastic training environments out of 1,024). CAD replay frame alignment was
exact, and all 73 sampled v5 CAD poses passed the overlap check. The Python suite
passed 28 tests, including the hybrid contract and IK regression.

This qualification covers nominal flat ground and initial-pose variation only.
The largest joint speed sampled across the two sets was 8.61 rad/s; the 2 rad/s
reference is still a soft penalty. Servo speed/torque behavior, latency, printed
mass properties, and contact friction need measurement and randomization before
hardware transfer. The checkpoint requires the complete hybrid controller;
exporting the neural network alone would omit its nominal gait and feedback.

The IK sweep also now handles near-zero warm starts explicitly. Previously,
SciPy's trust-region solver could start with an almost-zero trust radius at a
neutral crossing and stop before reaching a feasible target. This falsely
reported a 3.125 mm reachability error for the wider lift sweep. The regression
test verifies the 25 mm trajectory within a 0.1 mm positional tolerance.

## View walking with CAD meshes

Record a short rollout of the selected policy and render the original CAD
chassis, motors, brackets, and printable legs:

```bash
./scripts/run_spider_rl.sh preview \
  --checkpoint /workspace/output/spider_rl/walk/<run>/model_500.pt \
  --seconds 10 --seed 50000 --video
```

This prints `PREVIEW_DIR` beneath the checkpoint's ignored `previews/` folder,
containing `trajectory.npz`, `preview.json`, and optionally `cad_walking.mp4`
and `preview.png`. Open that trajectory in an interactive MuJoCo desktop window:

```bash
./scripts/run_spider_rl.sh view --detach \
  --trajectory /workspace/output/spider_rl/walk/<run>/previews/<preview>/trajectory.npz
```

Use the mouse to orbit/zoom, Space to pause/play, and R to restart. Playback loops
and the camera follows the robot. Close the window to stop it. `view` requires
the same X11/XWayland desktop setup as the CAD viewer; omit `--detach` to keep
logs attached to the terminal.

The native viewer also needs an active desktop monitor. A reachable X server
with zero monitors is insufficient; the viewer reports this condition and exits.
Policy recording with `preview --video` and training capture with `watch-video`
use offscreen rendering and still work.

Physics and inference run in the unchanged GPU training task. A separate CPU
model displays the recorded poses with CAD visual meshes and no robot collision
meshes. Recording verifies checkpoint/CAD hashes, maps joints by name, checks
CAD versus training body positions to 1 μm at one-second intervals, and rejects
terminated episodes. Viewing rechecks CAD hashes. This is a visual replay,
not a closed-loop CPU policy test or evidence that CAD mesh contacts work.
Neither command opens robot devices or uses the Joshua launcher.

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
contracts extend the table above with their explicit gait-clock definition and
action scale: v1/v2 use ±0.2 rad, and v3/v4 use ±0.3 rad. V5 uses ±0.08 rad
residuals around its nominal controller, with combined targets limited to ±0.4 rad.
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
- `mjlab_joshua/gait.py` — versioned reachable foot-path guide, measured clearance,
  support, and contact-point slip objectives.
- `mjlab_joshua/hybrid.py` — v5 trajectory prior, outer tracking feedback,
  bounded PPO residuals, and accepted-residual observations.
- `mjlab_joshua/preview.py` — GPU policy pose recording, detailed CAD video, and
  interactive MuJoCo visual replay.
- `mjlab_joshua/live.py` — training-state publication and interactive multi-robot
  CAD visualization with live progress.
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
