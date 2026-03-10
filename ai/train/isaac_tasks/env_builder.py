"""Generic Isaac Lab environment config builder.

Dynamically constructs ``ManagerBasedRLEnvCfg`` subclasses from plain
dicts of term configs -- no per-task configclass boilerplate needed.

Usage::

    from isaac_tasks.env_builder import build_env_cfg, build_scene_cfg
    from isaac_tasks import terms

    AntEnvCfg = build_env_cfg(
        scene_cfg=build_scene_cfg(robot=ANT_CFG, num_envs=4096),
        actions_cfg=terms.joint_effort_actions(scale=7.5),
        rewards={"alive": terms.is_alive(weight=0.5), ...},
        observations={"base_height": terms.base_pos_z(), ...},
        terminations={"time_out": terms.time_out(), ...},
        events={"reset_base": terms.reset_root_state(), ...},
    )
"""

from __future__ import annotations

import os

import isaaclab.sim as sim_utils
from isaaclab.actuators import ImplicitActuatorCfg
from isaaclab.assets import ArticulationCfg, AssetBaseCfg
from isaaclab.envs import ManagerBasedRLEnvCfg
from isaaclab.managers import ObservationGroupCfg as ObsGroup
from isaaclab.scene import InteractiveSceneCfg
from isaaclab.terrains import TerrainImporterCfg
from isaaclab.utils import configclass

_MODELS_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..",
                 "simulation", "models")
)


def _make_configclass(name: str, terms: dict):
    """Create a ``@configclass`` from a ``{name: TermCfg}`` dict."""
    ns = dict(terms)
    ns["__annotations__"] = {k: type(v) for k, v in terms.items()}
    return configclass(type(name, (), ns))


def resolve_model_path(filename: str) -> str:
    """Resolve a model filename to an absolute path.

    Looks in ``simulation/models/`` relative to the Joshua workspace.
    The file-existence check is deferred: a warning is logged at import
    time but the error only triggers when Isaac Lab actually loads the
    USD (so task modules can be imported for registration even before
    assets exist on disk).
    """
    path = os.path.join(_MODELS_DIR, filename)
    if not os.path.isfile(path):
        import logging
        logging.getLogger(__name__).warning(
            "Model not found at %s -- Isaac Lab will fail when loading "
            "the scene.  Make sure the file exists.", path,
        )
    return path


def build_robot_from_usd(
    usd_path: str,
    init_pos: tuple = (0.0, 0.0, 0.5),
    init_joint_pos: dict | None = None,
    actuator_stiffness: float = 0.0,
    actuator_damping: float = 0.0,
) -> ArticulationCfg:
    """Build an ``ArticulationCfg`` from a local USD file.

    Use this when the robot USD is stored in the Joshua repo under
    ``simulation/models/``.

    Args:
        usd_path: Absolute path to a ``.usd`` file.
        init_pos: Initial root position ``(x, y, z)``.
        init_joint_pos: Optional joint position overrides ``{"pattern": value}``.
        actuator_stiffness: Joint stiffness (0 = pure torque control).
        actuator_damping: Joint damping.
    """
    return ArticulationCfg(
        prim_path="{ENV_REGEX_NS}/Robot",
        spawn=sim_utils.UsdFileCfg(
            usd_path=usd_path,
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
            pos=init_pos,
            joint_pos=init_joint_pos or {},
        ),
        actuators={
            "body": ImplicitActuatorCfg(
                joint_names_expr=[".*"],
                stiffness=actuator_stiffness,
                damping=actuator_damping,
            ),
        },
    )


def build_scene_cfg(
    robot,
    num_envs: int = 4096,
    env_spacing: float = 5.0,
    terrain_friction: float = 1.0,
):
    """Build a standard scene with flat terrain, robot, and distant light.

    Args:
        robot: An Isaac Lab ``ArticulationCfg`` (e.g. ``ANT_CFG``).
        num_envs: Number of parallel environments.
        env_spacing: Spacing between environment origins.
        terrain_friction: Static and dynamic friction for the ground plane.
    """
    terrain = TerrainImporterCfg(
        prim_path="/World/ground",
        terrain_type="plane",
        collision_group=-1,
        physics_material=sim_utils.RigidBodyMaterialCfg(
            friction_combine_mode="average",
            restitution_combine_mode="average",
            static_friction=terrain_friction,
            dynamic_friction=terrain_friction,
            restitution=0.0,
        ),
        debug_vis=False,
    )
    robot_cfg = robot.replace(prim_path="{ENV_REGEX_NS}/Robot")
    light = AssetBaseCfg(
        prim_path="/World/light",
        spawn=sim_utils.DistantLightCfg(
            color=(0.75, 0.75, 0.75), intensity=3000.0,
        ),
    )

    ns = {
        "terrain": terrain,
        "robot": robot_cfg,
        "light": light,
        "__annotations__": {
            "terrain": type(terrain),
            "robot": type(robot_cfg),
            "light": type(light),
        },
    }
    SceneCfg = configclass(type("SceneCfg", (InteractiveSceneCfg,), ns))
    return SceneCfg(num_envs=num_envs, env_spacing=env_spacing)


def build_env_cfg(
    scene_cfg,
    actions_cfg,
    rewards: dict,
    observations: dict,
    terminations: dict,
    events: dict,
    decimation: int = 2,
    episode_length_s: float = 16.0,
    sim_dt: float = 1.0 / 120.0,
) -> type:
    """Build a ``ManagerBasedRLEnvCfg`` subclass from dicts of terms.

    Returns the **class** (not an instance).  Isaac Lab's
    ``parse_env_cfg`` will instantiate it and override ``num_envs``.

    Args:
        scene_cfg: Scene config instance (use ``build_scene_cfg``).
        actions_cfg: Actions config instance (use ``terms.joint_effort_actions``).
        rewards: ``{"name": RewardTermCfg, ...}``
        observations: ``{"name": ObservationTermCfg, ...}`` (policy group).
        terminations: ``{"name": TerminationTermCfg, ...}``
        events: ``{"name": EventTermCfg, ...}``
        decimation: Action repeat / physics sub-steps per env step.
        episode_length_s: Maximum episode length in seconds.
        sim_dt: Simulation timestep.
    """
    RewardsCfg = _make_configclass("RewardsCfg", rewards)
    TerminationsCfg = _make_configclass("TerminationsCfg", terminations)
    EventsCfg = _make_configclass("EventsCfg", events)

    obs_ns = dict(observations)
    obs_ns["__annotations__"] = {k: type(v) for k, v in observations.items()}

    def _obs_post_init(self):
        self.enable_corruption = False
        self.concatenate_terms = True

    obs_ns["__post_init__"] = _obs_post_init
    PolicyCfg = configclass(type("PolicyCfg", (ObsGroup,), obs_ns))

    ObsCfg = _make_configclass("ObservationsCfg", {"policy": PolicyCfg()})

    _dec = decimation
    _eps = episode_length_s
    _dt = sim_dt

    def _env_post_init(self):
        self.decimation = _dec
        self.episode_length_s = _eps
        self.sim.dt = _dt
        self.sim.render_interval = self.decimation
        self.sim.physx.bounce_threshold_velocity = 0.2
        self.sim.physics_material.static_friction = 1.0
        self.sim.physics_material.dynamic_friction = 1.0
        self.sim.physics_material.restitution = 0.0

    env_fields = {
        "scene": scene_cfg,
        "actions": actions_cfg,
        "rewards": RewardsCfg(),
        "observations": ObsCfg(),
        "terminations": TerminationsCfg(),
        "events": EventsCfg(),
    }
    env_ns = dict(env_fields)
    env_ns["__annotations__"] = {k: type(v) for k, v in env_fields.items()}
    env_ns["__post_init__"] = _env_post_init

    return configclass(type("EnvCfg", (ManagerBasedRLEnvCfg,), env_ns))
