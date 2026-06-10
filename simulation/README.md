# Simulation

Joshua supports two simulation backends, both launched through the unified
`joshua_main` launcher with a `MODE_SIMULATION` preset:

| Backend | Engine | Use case |
|---------|--------|----------|
| `SIM_BACKEND_MUJOCO` (default) | MuJoCo | Interactive viewing, trajectory playback, hardware mirroring, offscreen rendering |
| `SIM_BACKEND_ISAAC_SIM` | NVIDIA Isaac Sim | GPU-accelerated PhysX simulation of USD robot assets |

## Directory layout

```
simulation/
├── main.py             # entry point: loads config, dispatches to a backend
├── proto/              # SimulationConfig schema
├── mujoco/             # MuJoCo backend (in-process)
│   ├── engine.py       #   model/data wrapper
│   └── modes/          #   interactive · passive · mirror · offscreen
├── isaac/              # Isaac Sim backend (subprocess)
│   ├── launcher.py     #   Bazel-side: config → JSON, spawns the viewer
│   └── viewer.py       #   Isaac-venv-side: USD scene + physics loop
└── models/             # robot assets, one directory per robot
    ├── so_arm100/      #   MuJoCo XMLs, STL meshes, demo trajectories
    ├── ant/            #   ant.xml (MuJoCo) + ant_isaac.usda + README
    ├── trileg/         #   trileg_isaac.usda + README
    └── bileg/          #   bileg_isaac.usda
```

## MuJoCo backend

MuJoCo runs in-process (Bazel-managed Python) and supports four modes,
selected via `simulation.mode` in the preset:

| Mode | Description |
|------|-------------|
| `MODE_INTERACTIVE` | Interactive 3D viewer |
| `MODE_PASSIVE` | Trajectory playback from a CSV file |
| `MODE_MIRROR` | Mirror live hardware encoder topics into the sim |
| `MODE_OFFSCREEN` | Headless frame rendering to disk |

```bash
# SO-ARM100 interactive viewer
bazel run //launcher:joshua_main -- \
    --config config/config_preset/so100/sim_interactive.pbtxt

# Ant interactive viewer
bazel run //launcher:joshua_main -- \
    --config config/config_preset/ant/ant_sim_interactive.pbtxt
```

Robot models (MuJoCo XML) live in [models/](models/) under each robot's
directory; `model_path` in the preset points at the scene XML, e.g.
`simulation/models/so_arm100/so_arm100_scene.xml`.

## Isaac Sim backend

Isaac Sim bundles its own Python 3.11 and numpy<2, which cannot coexist with
Joshua's Bazel-managed Python 3.10 in one process. Joshua therefore runs
Isaac Sim in a **subprocess** with its own virtual environment:

```
.pbtxt preset (MODE_SIMULATION, backend: SIM_BACKEND_ISAAC_SIM)
    │
    ▼
joshua_main (C++)  →  simulation/main.py (Bazel py3.10)
    │
    ▼
simulation/isaac/launcher.py   serializes config → JSON, spawns:
    │
    ▼
simulation/isaac/viewer.py     (Isaac Lab venv, py3.11)
    loads the USD into a flat-terrain scene and steps physics
```

### Prerequisites

1. **Install Isaac Sim** (4.5+ recommended, NVIDIA GPU required):
   https://docs.isaacsim.omniverse.nvidia.com/latest/installation/index.html

2. **Clone and install Isaac Lab**:

```bash
cd ~
git clone https://github.com/isaac-sim/IsaacLab.git
cd IsaacLab
./isaaclab.sh --install
```

3. **Set environment variables**:

```bash
export ISAAC_LAB_PATH=~/IsaacLab
export ISAAC_LAB_PYTHON=~/env_isaaclab/bin/python   # optional, overrides isaaclab.sh
```

### Running

```bash
# Ant
bazel run //launcher:joshua_main -- \
    --config config/config_preset/ant/ant_sim_isaac.pbtxt

# Trileg (3-legged LEGO walker)
bazel run //launcher:joshua_main -- \
    --config config/config_preset/trileg/trileg_sim_isaac.pbtxt

# Bileg (2-legged LEGO walker)
bazel run //launcher:joshua_main -- \
    --config config/config_preset/bileg/bileg_sim_isaac.pbtxt
```

Close the viewer window (or Ctrl+C) to exit.

### Configuration (`IsaacSimConfig`)

| Field | Default | Description |
|-------|---------|-------------|
| `usd_filename` | (required) | USD asset relative to `simulation/models/` (e.g. `"ant/ant_isaac.usda"`) |
| `init_pos_x/y/z` | 0 | Initial root position |
| `init_joint_pos` | {} | Joint positions keyed by name regex |
| `actuator_stiffness` | 0 | Joint drive stiffness (0 = pure torque) |
| `actuator_damping` | 0 | Joint drive damping |
| `sim_dt` | 1/120 s | Physics timestep |
| `headless` | false | Run without the viewer window |

### Available USD robots

| Robot | DOF | USD | Documentation |
|-------|-----|-----|---------------|
| Ant | 8 (4 legs x 2 joints) | `models/ant/ant_isaac.usda` | [models/ant/README.md](models/ant/README.md) |
| Trileg | 6 (3 legs x 2 joints) | `models/trileg/trileg_isaac.usda` | [models/trileg/README.md](models/trileg/README.md) |
| Bileg | 4 | `models/bileg/bileg_isaac.usda` | -- |

### Troubleshooting

| Problem | Fix |
|---------|-----|
| `OSError: Isaac Lab not found` | Set `ISAAC_LAB_PATH` or `ISAAC_LAB_PYTHON` |
| `Command failed to spawn: Aborted` | Check `nvidia-smi`; verify GPU driver compatibility |
| `ModuleNotFoundError: No module named 'pxr'` | Expected inside Bazel; USD tools only work in the Isaac Lab venv |
