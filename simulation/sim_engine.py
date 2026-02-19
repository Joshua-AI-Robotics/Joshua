"""Shared MuJoCo simulation engine.

Wraps the MuJoCo model + data lifecycle and provides the API that all
simulation modes use.
"""

from __future__ import annotations

from typing import Optional

import glog
import mujoco
import numpy as np

from simulation.proto import simulation_pb2


class SimEngine:
    """Owns a MuJoCo model/data pair and exposes a clean control API."""

    def __init__(self, config: simulation_pb2.SimulationConfig) -> None:
        self._config = config
        self._model = mujoco.MjModel.from_xml_path(config.model_path)
        self._data = mujoco.MjData(self._model)
        self._renderer: Optional[mujoco.Renderer] = None

        mujoco.mj_forward(self._model, self._data)

        glog.info(f"SimEngine loaded: {config.model_path}")
        glog.info(
            f"  bodies={self._model.nbody}  joints={self._model.njnt}  "
            f"actuators={self._model.nu}  sensors={self._model.nsensor}"
        )

    @property
    def config(self) -> simulation_pb2.SimulationConfig:
        return self._config

    @property
    def model(self) -> mujoco.MjModel:
        return self._model

    @property
    def data(self) -> mujoco.MjData:
        return self._data

    @property
    def num_actuators(self) -> int:
        return self._model.nu

    @property
    def timestep(self) -> float:
        return self._model.opt.timestep

    def step(self, n: int = 1) -> None:
        for _ in range(n):
            mujoco.mj_step(self._model, self._data)

    def set_ctrl(self, index: int, value: float) -> None:
        if 0 <= index < self._model.nu:
            self._data.ctrl[index] = value

    def get_qpos(self, joint_name: str) -> float:
        jnt_id = mujoco.mj_name2id(self._model, mujoco.mjtObj.mjOBJ_JOINT, joint_name)
        if jnt_id < 0:
            raise KeyError(f"Joint '{joint_name}' not found in model")
        return float(self._data.qpos[self._model.jnt_qposadr[jnt_id]])

    def get_sensor(self, sensor_name: str) -> float:
        sid = mujoco.mj_name2id(self._model, mujoco.mjtObj.mjOBJ_SENSOR, sensor_name)
        if sid < 0:
            raise KeyError(f"Sensor '{sensor_name}' not found in model")
        return float(self._data.sensordata[self._model.sensor_adr[sid]])

    def render(
        self, width: int = 640, height: int = 480, camera: str = "front"
    ) -> np.ndarray:
        """Return an RGB image (H, W, 3) from the named camera."""
        if self._renderer is None:
            self._renderer = mujoco.Renderer(self._model, height=height, width=width)
        elif self._renderer.height != height or self._renderer.width != width:
            self._renderer.close()
            self._renderer = mujoco.Renderer(self._model, height=height, width=width)

        cam_id = mujoco.mj_name2id(self._model, mujoco.mjtObj.mjOBJ_CAMERA, camera)
        if cam_id < 0:
            raise KeyError(f"Camera '{camera}' not found in model")

        self._renderer.update_scene(self._data, camera=cam_id)
        return self._renderer.render()

    def reset(self) -> None:
        mujoco.mj_resetData(self._model, self._data)
        mujoco.mj_forward(self._model, self._data)

    def close(self) -> None:
        if self._renderer is not None:
            self._renderer.close()
            self._renderer = None

    def __del__(self) -> None:
        self.close()
