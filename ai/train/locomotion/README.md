# Spider locomotion training foundation

This isolated Python 3.12 environment brings the [twelve-motor STS3215 spider](../../../tools/cad/SPIDER_STS3215.md)
into mjlab for batched MuJoCo physics and PPO. The first task holds a standing
pose at zero commanded velocity. It follows MicroDuck's progression from a
verified robot model to batched RL, policy export, CPU replay, and hardware
qualification. Walking training and Joshua inference integration are later
steps; the [Joshua simulation backend](../../../simulation/README.md) currently
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
Toolkit configured for Docker and defaults to 64 GPU environments. Verify the
container runtime using the [host setup guide](../../../docs/GETTING_STARTED.md)
and `docker compose run --rm joshua-u24 nvidia-smi`. The error
`could not select device driver ... capabilities: [[gpu]]` indicates missing
Docker GPU support even when host `nvidia-smi` works. The CPU smoke does not
establish GPU compatibility or throughput. No automatic CPU fallback occurs.

Each smoke holds a zero target for three seconds, checks action saturation and
reset feedback, then runs five PPO iterations with 24 policy steps per update.
It saves checkpoints, normalization state, TensorBoard events, a contract, and
`smoke.json` in a new `output/spider_rl/runs/<UTC timestamp>/` directory.
Five iterations check the pipeline; they do not produce a walking controller.
Logging stays local and no W&B credentials are required.

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

The generated `policy_contract.json` records asset/source hashes, joint order,
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

The actor receives no base linear velocity or contact ground truth. Hardware will
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
| This foundation | Isolated stack, primitive model, policy contract, standing and PPO smoke |
| Locomotion | Low-speed commands/curriculum, gait rewards, seeded evaluations and videos; benchmark 512–1024 envs before scaling |
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
- A trained walking policy, ONNX export, or a Joshua ROS policy adapter in this stage.
- Reusing MicroDuck's XL330 actuator model for STS3215 motors.

## References

- [Pollen Robotics MicroDuck RL](https://github.com/pollen-robotics/microduck_rl):
  mjlab/PPO training, actuator identification, export, and runtime rehearsal.
- [mjlab 1.3.0](https://github.com/mujocolab/mjlab/tree/v1.3.0):
  manager-based environments, MuJoCo-Warp physics, and RSL-RL integration.
