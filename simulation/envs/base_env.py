"""Model-agnostic and task-agnostic Gymnasium environment for MuJoCo."""

from __future__ import annotations

from typing import Any, Dict, Optional, Tuple

import gymnasium
import mujoco
import numpy as np
from gymnasium import spaces

from simulation.envs.task import Task


class MuJoCoEnv(gymnasium.Env):
    """Wraps a MuJoCo model and delegates reward / termination to a Task."""

    metadata = {"render_modes": ["human", "rgb_array"], "render_fps": 50}

    def __init__(
        self,
        model_path: str,
        task: Task,
        frame_skip: int = 10,
        camera_name: Optional[str] = None,
        image_obs: bool = False,
        render_mode: Optional[str] = None,
        render_width: int = 640,
        render_height: int = 480,
    ) -> None:
        super().__init__()
        self.render_mode = render_mode

        self._model = mujoco.MjModel.from_xml_path(model_path)
        self._data = mujoco.MjData(self._model)
        self._task = task
        self._frame_skip = frame_skip
        self._camera_name = camera_name
        self._image_obs = image_obs
        self._render_width = render_width
        self._render_height = render_height
        self._renderer: Optional[mujoco.Renderer] = None
        self._viewer: Any = None

        mujoco.mj_forward(self._model, self._data)

        nq = self._model.nq
        nv = self._model.nv
        nu = self._model.nu

        ctrl_low = self._model.actuator_ctrlrange[:, 0].copy()
        ctrl_high = self._model.actuator_ctrlrange[:, 1].copy()
        self.action_space = spaces.Box(low=ctrl_low, high=ctrl_high, dtype=np.float64)

        task_sample = task.get_task_obs(self._model, self._data)
        obs_dict: Dict[str, spaces.Space] = {
            "qpos": spaces.Box(-np.inf, np.inf, shape=(nq,), dtype=np.float64),
            "qvel": spaces.Box(-np.inf, np.inf, shape=(nv,), dtype=np.float64),
        }
        for key, val in task_sample.items():
            obs_dict[key] = spaces.Box(
                -np.inf, np.inf, shape=val.shape, dtype=np.float64
            )

        if self._image_obs:
            obs_dict["image"] = spaces.Box(
                0,
                255,
                shape=(self._render_height, self._render_width, 3),
                dtype=np.uint8,
            )

        self.observation_space = spaces.Dict(obs_dict)

    @property
    def model(self) -> mujoco.MjModel:
        return self._model

    @property
    def data(self) -> mujoco.MjData:
        return self._data

    @property
    def dt(self) -> float:
        return self._model.opt.timestep * self._frame_skip

    def _get_obs(self) -> Dict[str, np.ndarray]:
        obs: Dict[str, np.ndarray] = {
            "qpos": self._data.qpos.copy(),
            "qvel": self._data.qvel.copy(),
        }
        obs.update(self._task.get_task_obs(self._model, self._data))

        if self._image_obs:
            obs["image"] = self._render_offscreen()

        return obs

    def step(
        self, action: np.ndarray
    ) -> Tuple[Dict[str, np.ndarray], float, bool, bool, Dict[str, Any]]:
        np.copyto(self._data.ctrl, action)
        mujoco.mj_step(self._model, self._data, nstep=self._frame_skip)

        obs = self._get_obs()
        reward = self._task.compute_reward(obs, action)
        terminated = self._task.is_terminated(obs)
        truncated = False
        info: Dict[str, Any] = {}

        if self.render_mode == "human":
            self.render()

        return obs, reward, terminated, truncated, info

    def reset(
        self,
        *,
        seed: Optional[int] = None,
        options: Optional[Dict[str, Any]] = None,
    ) -> Tuple[Dict[str, np.ndarray], Dict[str, Any]]:
        super().reset(seed=seed, options=options)

        mujoco.mj_resetData(self._model, self._data)
        self._task.reset(self._model, self._data)
        mujoco.mj_forward(self._model, self._data)

        obs = self._get_obs()

        if self.render_mode == "human":
            self.render()

        return obs, {}

    def render(self) -> Optional[np.ndarray]:
        if self.render_mode == "rgb_array":
            return self._render_offscreen()
        if self.render_mode == "human":
            self._render_human()
            return None
        return None

    def _render_offscreen(self) -> np.ndarray:
        if self._renderer is None:
            self._renderer = mujoco.Renderer(
                self._model,
                height=self._render_height,
                width=self._render_width,
            )

        if self._camera_name:
            cam_id = mujoco.mj_name2id(
                self._model, mujoco.mjtObj.mjOBJ_CAMERA, self._camera_name
            )
            self._renderer.update_scene(self._data, camera=cam_id)
        else:
            self._renderer.update_scene(self._data)

        return self._renderer.render()

    def _render_human(self) -> None:
        if self._viewer is None:
            import mujoco.viewer

            self._viewer = mujoco.viewer.launch_passive(self._model, self._data)
        else:
            self._viewer.sync()

    def close(self) -> None:
        if self._renderer is not None:
            self._renderer.close()
            self._renderer = None
        if self._viewer is not None:
            self._viewer.close()
            self._viewer = None
