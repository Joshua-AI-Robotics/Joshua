"""Publish live training poses and inspect a CAD grid in a separate viewer."""

import argparse
import hashlib
import json
import math
import os
import time
from pathlib import Path

import mujoco
import numpy as np

from .contract import JOINT_NAMES
from .preview import require_desktop, visual_model


def pose_indices(model, prefix=""):
    """Canonical pose: one free base followed by the twelve named joints."""
    free = np.flatnonzero(model.jnt_type == mujoco.mjtJoint.mjJNT_FREE)
    if len(free) != 1:
        raise ValueError("Live view requires exactly one floating-base robot")
    start = int(model.jnt_qposadr[free[0]])
    return np.array(
        list(range(start, start + 7))
        + [int(model.joint(prefix + name).qposadr[0]) for name in JOINT_NAMES]
    )


def write_snapshot(path, poses, metadata):
    """Atomically replace the frame so a viewer never sees a partial write."""
    temporary = path.with_suffix(".tmp")
    with temporary.open("wb") as stream:
        np.savez(stream, poses=poses, metadata=json.dumps(metadata))
    os.replace(temporary, path)


def live_wrapper(env, clip_actions, directory, count, fps, iterations):
    """Sample actual training states after steps; never change returned tensors."""
    import torch
    from mjlab.rl import RslRlVecEnvWrapper

    class LiveWrapper(RslRlVecEnvWrapper):
        def __init__(self):
            super().__init__(env, clip_actions=clip_actions)
            self.path = directory / "live.npz"
            self.task_id = json.loads((directory / "policy_contract.json").read_text())[
                "task_id"
            ]
            self.ids = torch.linspace(
                0, env.num_envs - 1, min(count, env.num_envs), device=env.device
            ).long()
            self.indices = torch.as_tensor(
                pose_indices(env.sim.mj_model, "robot/"), device=env.device
            )
            self.next_frame = 0.0
            self.iteration = lambda: 0
            self.start_iteration = 0
            self.publish("starting", force=True)

        def publish(self, status="training", force=False):
            now = time.monotonic()
            if not force and now < self.next_frame:
                return
            # Copy only the displayed subset from GPU. Each pose is relative to
            # its own environment origin; the viewer arranges these into a grid.
            poses = env.sim.data.qpos[self.ids][:, self.indices].cpu().numpy()
            poses[:, :3] -= env.scene.env_origins[self.ids].cpu().numpy()
            metadata = {
                "status": status,
                "task_id": self.task_id,
                "unix_time": time.time(),
                "num_envs": env.num_envs,
                "env_ids": self.ids.tolist(),
                "iteration": int(self.iteration()),
                "start_iteration": self.start_iteration,
                "planned_updates": iterations,
                "policy_steps": env.common_step_counter,
                "simulation_dt": env.step_dt,
                "mode": "live stochastic PPO training states",
            }
            write_snapshot(self.path, poses, metadata)
            self.next_frame = now + 1 / fps

        def step(self, actions):
            result = super().step(actions)
            self.publish()
            return result

    return LiveWrapper()


def read_snapshot(path):
    """Validate the local numeric frame before handing it to MuJoCo."""
    with np.load(path, allow_pickle=False) as saved:
        poses = saved["poses"]
        metadata = json.loads(str(saved["metadata"]))
    if poses.ndim != 2 or poses.shape[1] != 19 or not 1 <= len(poses) <= 64:
        raise ValueError("Invalid live pose dimensions")
    if not np.isfinite(poses).all():
        raise ValueError("Non-finite live pose")
    return poses, metadata


def set_pose(model, data, indices, pose, offset):
    """Forward kinematics only; the displayed model does not advance physics."""
    data.qpos[indices] = pose
    data.qpos[indices[:2]] += offset
    mujoco.mj_forward(model, data)


def grid_offsets(count):
    """Arrange independent training worlds into a compact viewing grid."""
    side = math.ceil(math.sqrt(count))
    return (
        np.array([(i // side, i % side) for i in range(count)]) * 1.0 - (side - 1) / 2
    )


def add_robots(model, data, indices, poses, offsets, scene, option):
    """Append other robots using shared CAD mesh assets, excluding their floors."""
    perturbation = mujoco.MjvPerturb()
    for pose, offset in zip(poses[1:], offsets[1:]):
        set_pose(model, data, indices, pose, offset)
        mujoco.mjv_addGeoms(
            model, data, option, perturbation, mujoco.mjtCatBit.mjCAT_DYNAMIC, scene
        )


def record_grid(model, path, output, seconds):
    """Capture actual training snapshots without requiring a desktop monitor."""
    import imageio.v2 as imageio
    from PIL import Image, ImageDraw, ImageFont

    data, extra = mujoco.MjData(model), mujoco.MjData(model)
    indices, option = pose_indices(model), mujoco.MjvOption()
    camera = mujoco.MjvCamera()
    camera.lookat[:] = [0.15, 0, 0.05]
    camera.azimuth, camera.elevation = 135, -55
    font = ImageFont.load_default(size=20)
    output.parent.mkdir(parents=True, exist_ok=True)
    frames = []
    with mujoco.Renderer(model, height=900, width=1200) as renderer:
        with imageio.get_writer(
            output, fps=10, codec="libx264", macro_block_size=4
        ) as writer:
            started = time.monotonic()
            while time.monotonic() - started < seconds:
                tick = time.monotonic()
                poses, metadata = read_snapshot(path)
                offsets = grid_offsets(len(poses))
                camera.distance = 1.6 * math.ceil(math.sqrt(len(poses)))
                set_pose(model, data, indices, poses[0], offsets[0])
                renderer.update_scene(data, camera=camera)
                add_robots(
                    model, extra, indices, poses, offsets, renderer.scene, option
                )
                frame = Image.fromarray(renderer.render())
                draw = ImageDraw.Draw(frame)
                draw.rectangle((0, 0, 1200, 58), fill="black")
                age = max(0, time.time() - metadata["unix_time"])
                draw.text(
                    (12, 6),
                    f"{metadata['task_id']} | {metadata['status']} | "
                    f"update {metadata['iteration']} | "
                    f"{len(poses)} shown / {metadata['num_envs']} training\n"
                    "Actual PPO state samples in wall-clock time | "
                    f"frame age {age:.1f}s",
                    font=font,
                    fill="white",
                )
                writer.append_data(np.asarray(frame))
                frames.append(metadata)
                time.sleep(max(0, 0.1 - (time.monotonic() - tick)))
    output.with_suffix(".json").write_text(
        json.dumps({"run": str(path.parent), "frames": frames}, indent=2)
    )


def main():
    """Watch a run without blocking training when the window is closed/paused."""
    import mujoco.viewer

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument(
        "--project",
        type=Path,
        default=Path("/workspace/output/robotcad/spider_sts3215"),
    )
    parser.add_argument("--screenshot", type=Path)
    parser.add_argument("--video", type=Path)
    parser.add_argument("--seconds", type=float, default=10)
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or args.seconds <= 0:
        parser.error("recording duration must be positive and finite")
    if args.screenshot and args.video:
        parser.error("choose either a screenshot or a video")
    contract = json.loads((args.run / "policy_contract.json").read_text())
    for name, digest in contract["source_hashes"].items():
        if hashlib.sha256((args.project / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f"CAD differs from the training run: {name}")
    model = visual_model(args.project)
    data, extra = mujoco.MjData(model), mujoco.MjData(model)
    indices = pose_indices(model)
    path = args.run / "live.npz"
    if args.video:
        record_grid(model, path, args.video, args.seconds)
        return
    poses, metadata = read_snapshot(path)
    offsets = grid_offsets(len(poses))
    set_pose(model, data, indices, poses[0], offsets[0])
    if args.screenshot:
        camera = mujoco.MjvCamera()
        camera.lookat[:] = [0.15, 0, 0.05]
        camera.distance = 1.6 * math.ceil(math.sqrt(len(poses)))
        camera.azimuth, camera.elevation = 135, -55
        with mujoco.Renderer(model, height=900, width=1200) as renderer:
            renderer.update_scene(data, camera=camera)
            add_robots(
                model,
                extra,
                indices,
                poses,
                offsets,
                renderer.scene,
                mujoco.MjvOption(),
            )
            import imageio.v2 as imageio

            imageio.imwrite(args.screenshot, renderer.render())
        return
    require_desktop()
    state = {"paused": False, "grid": True, "selected": 0, "camera_dirty": True}

    def key(code):
        if code == 32:
            state["paused"] = not state["paused"]
        elif code in (ord("G"), ord("g")):
            state["grid"] = not state["grid"]
            state["camera_dirty"] = True
        elif code == 258:  # GLFW Tab
            state["selected"] = (state["selected"] + 1) % len(poses)

    print(
        "LIVE training view: G grid/single, Tab next robot, Space freeze view.",
        flush=True,
    )
    option = mujoco.MjvOption()
    with mujoco.viewer.launch_passive(
        model, data, key_callback=key, show_left_ui=False, show_right_ui=False
    ) as viewer:
        viewer.cam.azimuth, viewer.cam.elevation = 135, -55
        while viewer.is_running():
            started = time.monotonic()
            if not state["paused"]:
                poses, metadata = read_snapshot(path)
            if state["camera_dirty"]:
                viewer.cam.distance = (
                    1.6 * math.ceil(math.sqrt(len(poses))) if state["grid"] else 0.7
                )
                state["camera_dirty"] = False
            selected = 0 if state["grid"] else state["selected"]
            offset = offsets[selected] if state["grid"] else np.zeros(2)
            with viewer.lock():
                set_pose(model, data, indices, poses[selected], offset)
                viewer.user_scn.ngeom = 0
                if state["grid"]:
                    add_robots(
                        model, extra, indices, poses, offsets, viewer.user_scn, option
                    )
                    viewer.cam.lookat[:] = [0.15, 0, 0.05]
                else:
                    viewer.cam.lookat[:] = data.xpos[model.body("base_link").id]
            age = max(0, time.time() - metadata["unix_time"])
            status = metadata["status"].upper()
            if state["paused"]:
                status = "VIEW FROZEN (training continues)"
            elif status == "TRAINING" and age > 5:
                status = "WAITING FOR TRAINER"
            viewer.set_texts(
                (
                    mujoco.mjtFontScale.mjFONTSCALE_150,
                    mujoco.mjtGridPos.mjGRID_TOPLEFT,
                    "Live PPO training\nTask\nEnvironments\nIteration\n"
                    "Policy steps\nFrame age\nControls",
                    f"{status}\n{metadata.get('task_id', 'v1 baseline')}\n"
                    f"{len(poses)} shown / {metadata['num_envs']} training\n"
                    f"{metadata['iteration']} (start {metadata['start_iteration']}, "
                    f"budget {metadata['planned_updates']})\n"
                    f"{metadata['policy_steps']}\n"
                    f"{age:.1f} s\nG grid/single | Tab next | Space freeze view",
                )
            )
            viewer.sync()
            time.sleep(max(0, 1 / 20 - (time.monotonic() - started)))


if __name__ == "__main__":
    main()
