#!/usr/bin/env python3
"""Isaac Sim viewer for Joshua robot models.

This script runs inside Isaac Lab's Python environment (NOT Bazel).
It loads a robot USD into a flat-terrain scene and steps the physics
simulation until the viewer window is closed (or SIGINT in headless
mode). No RL, no gym -- just the simulator.

Communication with Joshua is via a JSON config file written by
``simulation/isaac/launcher.py``.

Usage (called automatically by the launcher):
    isaaclab.sh -p simulation/isaac/viewer.py --config /tmp/cfg.json
"""

from __future__ import annotations

import argparse
import json
import sys

# ── CLI + AppLauncher init (must happen before other Isaac imports) ────

parser = argparse.ArgumentParser(description="Joshua Isaac Sim viewer.")
parser.add_argument(
    "--config",
    required=True,
    help="Path to the JSON config written by simulation/isaac/launcher.py",
)

from isaaclab.app import AppLauncher  # noqa: E402

AppLauncher.add_app_launcher_args(parser)
args_cli = parser.parse_args()

app_launcher = AppLauncher(args_cli)
simulation_app = app_launcher.app

# ── Imports that require Isaac Sim to be running ──────────────────────

import isaaclab.sim as sim_utils  # noqa: E402
from isaaclab.actuators import ImplicitActuatorCfg  # noqa: E402
from isaaclab.assets import ArticulationCfg, AssetBaseCfg  # noqa: E402
from isaaclab.scene import InteractiveScene, InteractiveSceneCfg  # noqa: E402
from isaaclab.terrains import TerrainImporterCfg  # noqa: E402
from isaaclab.utils import configclass  # noqa: E402


def _build_robot_cfg(cfg: dict) -> ArticulationCfg:
    """Build an ``ArticulationCfg`` from the JSON viewer config."""
    init_pos = cfg.get("init_pos") or [0.0, 0.0, 0.3]
    return ArticulationCfg(
        prim_path="{ENV_REGEX_NS}/Robot",
        spawn=sim_utils.UsdFileCfg(
            usd_path=cfg["usd_path"],
            rigid_props=sim_utils.RigidBodyPropertiesCfg(
                disable_gravity=False,
                max_depenetration_velocity=10.0,
                enable_gyroscopic_forces=True,
            ),
            articulation_props=sim_utils.ArticulationRootPropertiesCfg(
                enabled_self_collisions=False,
                solver_position_iteration_count=4,
                solver_velocity_iteration_count=0,
                sleep_threshold=0.005,
                stabilization_threshold=0.001,
            ),
            copy_from_source=False,
        ),
        init_state=ArticulationCfg.InitialStateCfg(
            pos=tuple(init_pos),
            joint_pos=cfg.get("init_joint_pos") or {},
        ),
        actuators={
            "body": ImplicitActuatorCfg(
                joint_names_expr=[".*"],
                stiffness=cfg.get("actuator_stiffness", 0.0),
                damping=cfg.get("actuator_damping", 0.0),
            ),
        },
    )


def _build_scene_cfg(robot: ArticulationCfg) -> type:
    """Build a scene config class: flat terrain, robot, distant light."""
    terrain = TerrainImporterCfg(
        prim_path="/World/ground",
        terrain_type="plane",
        collision_group=-1,
        physics_material=sim_utils.RigidBodyMaterialCfg(
            friction_combine_mode="average",
            restitution_combine_mode="average",
            static_friction=1.0,
            dynamic_friction=1.0,
            restitution=0.0,
        ),
        debug_vis=False,
    )
    light = AssetBaseCfg(
        prim_path="/World/light",
        spawn=sim_utils.DistantLightCfg(
            color=(0.75, 0.75, 0.75),
            intensity=3000.0,
        ),
    )

    ns = {
        "terrain": terrain,
        "robot": robot,
        "light": light,
        "__annotations__": {
            "terrain": type(terrain),
            "robot": type(robot),
            "light": type(light),
        },
    }
    return configclass(type("ViewerSceneCfg", (InteractiveSceneCfg,), ns))


def main() -> None:
    with open(args_cli.config) as f:
        cfg = json.load(f)

    sim_dt = cfg.get("sim_dt") or 1.0 / 120.0
    sim = sim_utils.SimulationContext(sim_utils.SimulationCfg(dt=sim_dt))
    sim.set_camera_view(eye=(3.0, 3.0, 2.0), target=(0.0, 0.0, 0.5))

    scene_cfg_cls = _build_scene_cfg(_build_robot_cfg(cfg))
    scene = InteractiveScene(scene_cfg_cls(num_envs=1, env_spacing=2.0))

    sim.reset()
    print(f"[Joshua/Isaac] Viewer running: {cfg['usd_path']}")
    print(f"[Joshua/Isaac]   dt={sim_dt:.6f}s -- close the window or Ctrl+C to exit")

    while simulation_app.is_running():
        sim.step()
        scene.update(sim.get_physics_dt())


if __name__ == "__main__":
    import traceback as _tb

    _exit_code = 0
    try:
        main()
    except KeyboardInterrupt:
        pass
    except SystemExit as e:
        _exit_code = e.code if isinstance(e.code, int) else 1
    except Exception:
        print("[Joshua/Isaac] FATAL error in viewer:", file=sys.stderr)
        _tb.print_exc()
        _exit_code = 1
    finally:
        try:
            simulation_app.close()
        except SystemExit:
            pass
    sys.exit(_exit_code)
