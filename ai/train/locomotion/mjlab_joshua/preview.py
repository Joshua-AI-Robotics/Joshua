"""Record policy motion and replay it with the original CAD visual meshes."""

import argparse
import hashlib
import json
import os
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path

import imageio.v2 as imageio
import mujoco
import numpy as np


def require_desktop():
    """Fail clearly before GLFW's native viewer asserts on an absent monitor."""
    import glfw

    if not glfw.init():
        raise RuntimeError("Cannot connect to the desktop display; use --video.")
    try:
        if not glfw.get_monitors():
            raise RuntimeError(
                "The desktop reports no active monitors. Reconnect the display "
                "before opening the viewer; offscreen --video remains available."
            )
    finally:
        glfw.terminate()


def visual_model(project):
    """Load CAD appearance for kinematic playback, without collision geometry."""
    root = ET.parse(project / "mujoco/robot.xml").getroot()
    root.find("compiler").set("meshdir", str(project / "mujoco/meshes"))
    for body in root.iter("body"):
        for geom in list(body.findall("geom")):
            if geom.get("group") != "1":
                body.remove(geom)
    used = {g.get("mesh") for g in root.iter("geom")}
    asset = root.find("asset")
    for mesh in list(asset):
        if mesh.get("name") not in used:
            asset.remove(mesh)
    ET.SubElement(
        asset,
        "texture",
        name="ground",
        type="2d",
        builtin="checker",
        rgb1="0.23 0.27 0.31",
        rgb2="0.32 0.36 0.40",
        width="512",
        height="512",
    )
    ET.SubElement(asset, "material", name="ground", texture="ground", texrepeat="20 20")
    world = root.find("worldbody")
    ET.SubElement(world, "geom", type="plane", size="5 5 0.01", material="ground")
    ET.SubElement(world, "light", pos="0 -1 2", dir="0 0 -1", directional="true")
    visual = ET.SubElement(root, "visual")
    ET.SubElement(visual, "global", offwidth="1200", offheight="900")
    ET.SubElement(visual, "headlight", ambient="0.4 0.4 0.4", diffuse="0.6 0.6 0.6")
    return mujoco.MjModel.from_xml_string(ET.tostring(root, encoding="unicode"))


def position_indices(source, target):
    """Map named joints and the unique free base; never assume model ordering."""
    indices = np.empty(target.nq, dtype=int)
    for joint in range(target.njnt):
        name = target.joint(joint).name
        kind = target.jnt_type[joint]
        if kind == mujoco.mjtJoint.mjJNT_FREE:
            matches = np.flatnonzero(source.jnt_type == kind)
        else:
            matches = [
                j
                for j in range(source.njnt)
                if source.joint(j).name.rsplit("/", 1)[-1] == name
            ]
        if len(matches) != 1:
            raise ValueError(f"Cannot uniquely map CAD joint {name}")
        match = matches[0]
        if source.jnt_type[match] != kind:
            raise ValueError(f"Joint type mismatch: {name}")
        width = 7 if kind == mujoco.mjtJoint.mjJNT_FREE else 1
        start, dest = source.jnt_qposadr[match], target.jnt_qposadr[joint]
        indices[dest : dest + width] = np.arange(start, start + width)
    return indices


def camera_for(model, data):
    """Follow the root while preserving a useful view of all four legs."""
    camera = mujoco.MjvCamera()
    camera.distance, camera.azimuth, camera.elevation = 0.7, 135, -25
    camera.lookat[:] = data.xpos[model.body("base_link").id] + [0, 0, 0.03]
    return camera


def record(args):
    """Run the unmodified GPU task; render its poses separately using CAD meshes."""
    from dataclasses import asdict

    import torch
    from mjlab.envs import ManagerBasedRlEnv
    from mjlab.managers import EventTermCfg
    from mjlab.rl import MjlabOnPolicyRunner, RslRlVecEnvWrapper
    from tensordict import TensorDict

    from .evaluate import reset_cases
    from .locomotion import (
        gait_version_from_contract,
        make_walk_cfg,
        walk_rl_cfg,
        walking_contract,
    )
    from .train import verify_asset

    asset = Path(os.environ["JOSHUA_SPIDER_ASSET"])
    saved = json.loads((args.checkpoint.parent / "policy_contract.json").read_text())
    gait_version = gait_version_from_contract(saved)
    contract = walking_contract(verify_asset(asset), gait_version)
    if saved != contract:
        raise ValueError("Checkpoint contract differs from current asset")
    for name, digest in contract["source_hashes"].items():
        if hashlib.sha256((args.project / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f"CAD source differs from the training contract: {name}")
    cfg = make_walk_cfg(
        asset, 1, evaluation=True, speed=args.speed, gait_version=gait_version
    )
    cfg.seed = args.seed
    cfg.episode_length_s = args.seconds + 1
    cfg.events = {
        "cases": EventTermCfg(
            func=reset_cases,
            mode="reset",
            params={"seed_base": args.seed},
        )
    }
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    out = args.checkpoint.parent / "previews" / stamp
    out.mkdir(parents=True)
    model = visual_model(args.project)
    data = mujoco.MjData(model)
    env = ManagerBasedRlEnv(cfg=cfg, device="cuda:0")
    frames, max_error = [], 0.0
    try:
        runner = MjlabOnPolicyRunner(
            RslRlVecEnvWrapper(env, clip_actions=1.0),
            asdict(walk_rl_cfg(gait_version)),
            None,
            "cuda:0",
        )
        runner.load(str(args.checkpoint), map_location="cuda:0")
        policy = runner.get_inference_policy(device="cuda:0")
        obs, _ = env.reset(seed=args.seed)
        source = env.sim.mj_model
        source_data = mujoco.MjData(source)
        indices = position_indices(source, model)
        with torch.inference_mode():
            for step in range(round(args.seconds / env.step_dt) + 1):
                qpos = env.sim.data.qpos[0].cpu().numpy().copy()
                if not np.isfinite(qpos).all():
                    raise RuntimeError("Non-finite policy pose")
                frames.append(qpos[indices])
                if step % 50 == 0:
                    source_data.qpos[:] = qpos
                    data.qpos[:] = frames[-1]
                    mujoco.mj_forward(source, source_data)
                    mujoco.mj_forward(model, data)
                    for body in range(1, model.nbody):
                        other = source.body("robot/" + model.body(body).name).id
                        error = np.max(
                            np.abs(data.xpos[body] - source_data.xpos[other])
                        )
                        max_error = max(max_error, float(error))
                    if max_error > 1e-6:
                        raise RuntimeError(
                            f"CAD/training frame mismatch: {max_error} m"
                        )
                if step == round(args.seconds / env.step_dt):
                    break
                raw_actions = policy(TensorDict(obs, batch_size=[1]))
                if not torch.isfinite(raw_actions).all():
                    raise RuntimeError("Non-finite policy action")
                obs, _, done, timeout, _ = env.step(raw_actions.clamp(-1, 1))
                if (done | timeout).any():
                    raise RuntimeError(
                        "Preview case terminated; refusing to replay resets"
                    )
        poses = np.asarray(frames)
        np.savez_compressed(out / "trajectory.npz", qpos=poses, dt=env.step_dt)
    finally:
        env.close()
    report = {
        "mode": "CAD visual replay of GPU policy rollout; not CPU policy validation",
        "checkpoint": str(args.checkpoint),
        "seed": args.seed,
        "checkpoint_sha256": hashlib.sha256(args.checkpoint.read_bytes()).hexdigest(),
        "source_hashes": contract["source_hashes"],
        "command_m_s": args.speed,
        "seconds": args.seconds,
        "frames": len(poses),
        "dt": float(env.step_dt),
        "cad_frame_max_error_m": max_error,
        "cad_visual_meshes": int(model.nmesh),
    }
    (out / "preview.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PREVIEW_DIR={out}", flush=True)
    if args.video:
        with mujoco.Renderer(model, height=900, width=1200) as renderer:
            with imageio.get_writer(
                out / "cad_walking.mp4", fps=25, codec="libx264", macro_block_size=4
            ) as writer:
                for frame in range(round(args.seconds * 25)):
                    index = min(round(frame / 25 / env.step_dt), len(poses) - 1)
                    data.qpos[:] = poses[index]
                    mujoco.mj_forward(model, data)
                    renderer.update_scene(data, camera=camera_for(model, data))
                    pixels = renderer.render()
                    writer.append_data(pixels)
                    if frame == 25:
                        imageio.imwrite(out / "preview.png", pixels)


def play(args):
    """Loop recorded poses in an interactive viewer; Space pauses, R restarts."""
    import mujoco.viewer

    require_desktop()
    model = visual_model(args.project)
    data = mujoco.MjData(model)
    metadata = json.loads((args.trajectory.parent / "preview.json").read_text())
    for name, digest in metadata["source_hashes"].items():
        if hashlib.sha256((args.project / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f"CAD differs from the recorded preview: {name}")
    with np.load(args.trajectory, allow_pickle=False) as saved:
        poses, dt = saved["qpos"], float(saved["dt"])
    if poses.ndim != 2 or poses.shape[1] != model.nq or len(poses) < 2:
        raise ValueError("Trajectory dimensions do not match CAD model")
    if not np.isfinite(poses).all() or not np.isfinite(dt) or dt <= 0:
        raise ValueError("Invalid trajectory values or timing")
    state = {"paused": False, "elapsed": 0.0}

    def key(code):
        if code == 32:
            state["paused"] = not state["paused"]
        elif code in (ord("R"), ord("r")):
            state["elapsed"] = 0.0

    data.qpos[:] = poses[0]
    mujoco.mj_forward(model, data)
    print(
        "CAD policy replay: Space pause/play, R restart, mouse orbit/zoom.", flush=True
    )
    with mujoco.viewer.launch_passive(model, data, key_callback=key) as viewer:
        camera = camera_for(model, data)
        viewer.cam.distance = camera.distance
        viewer.cam.azimuth = camera.azimuth
        viewer.cam.elevation = camera.elevation
        viewer.cam.lookat[:] = camera.lookat
        last = time.monotonic()
        while viewer.is_running():
            now = time.monotonic()
            if not state["paused"]:
                state["elapsed"] += now - last
            last = now
            frame = int(state["elapsed"] / dt) % (len(poses) - 1)
            with viewer.lock():
                data.qpos[:] = poses[frame]
                data.time = frame * dt
                mujoco.mj_forward(model, data)
                viewer.cam.lookat[:] = data.xpos[model.body("base_link").id] + [
                    0,
                    0,
                    0.03,
                ]
            viewer.sync()
            time.sleep(1 / 60)


def main():
    """Keep render/replay outputs under the ignored training output tree."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("record", "play"))
    parser.add_argument(
        "--project",
        type=Path,
        default=Path("/workspace/output/robotcad/spider_sts3215"),
    )
    parser.add_argument("--checkpoint", type=Path)
    parser.add_argument("--trajectory", type=Path)
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--speed", type=float, default=0.05)
    parser.add_argument("--seed", type=int, default=50000)
    parser.add_argument("--video", action="store_true")
    args = parser.parse_args()
    if (
        args.seconds < 2
        or not np.isfinite(args.seconds)
        or args.speed <= 0
        or not np.isfinite(args.speed)
    ):
        parser.error("need a finite duration >=2 seconds and positive finite speed")
    if args.mode == "record":
        if args.checkpoint is None:
            parser.error("record requires --checkpoint")
        record(args)
    else:
        if args.trajectory is None:
            parser.error("play requires --trajectory")
        play(args)


if __name__ == "__main__":
    main()
