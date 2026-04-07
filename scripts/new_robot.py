#!/usr/bin/env python3
"""Scaffold a new robot for Joshua's Isaac Sim training pipeline.

Generates config presets, directory structure, and a starter README
from the template files in config/config_preset/.

Usage:
    python scripts/new_robot.py --name hexapod --usd hexapod_isaac.usda
    python scripts/new_robot.py --name hexapod  # defaults to <name>_isaac.usda
"""

import argparse
import os
import sys

REPO_ROOT = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..")
)
TEMPLATE_DIR = os.path.join(REPO_ROOT, "config", "config_preset")
PRESET_DIR = os.path.join(REPO_ROOT, "config", "config_preset")
MODELS_DIR = os.path.join(REPO_ROOT, "simulation", "models")


def _read_template(name: str) -> str:
    path = os.path.join(TEMPLATE_DIR, name)
    if not os.path.isfile(path):
        print(f"Error: template not found at {path}", file=sys.stderr)
        sys.exit(1)
    with open(path) as f:
        return f.read()


def _substitute(template: str, robot_name: str, usd_filename: str) -> str:
    return (
        template
        .replace("{{ROBOT_NAME}}", robot_name)
        .replace("{{USD_FILENAME}}", usd_filename)
    )


def _write_file(path: str, content: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)
    print(f"  Created: {os.path.relpath(path, REPO_ROOT)}")


def _generate_model_readme(robot_name: str, usd_filename: str) -> str:
    title = robot_name.replace("_", " ").title()
    return f"""{title}
{"=" * len(title)}

## Structure

TODO: Describe the robot's body and joint hierarchy.

## USD Asset

- File: `simulation/models/{usd_filename}`
- Articulation root: `{title}` (or your root prim name)
- Up axis: Z
- Units: meters

## Joints

| Joint name | Parent | Child | Type | Range |
|------------|--------|-------|------|-------|
| TODO       | TODO   | TODO  | revolute | TODO |

## Config Presets

- Training: `config/config_preset/{robot_name}/{robot_name}_train_isaac.pbtxt`
- Eval: `config/config_preset/{robot_name}/{robot_name}_eval_isaac.pbtxt`

## Quick Start

```bash
# Train
bazel run //launcher:joshua_main -- \\
    --config config/config_preset/{robot_name}/{robot_name}_train_isaac.pbtxt

# Evaluate (update checkpoint_path in the eval preset first)
bazel run //launcher:joshua_main -- \\
    --config config/config_preset/{robot_name}/{robot_name}_eval_isaac.pbtxt
```
"""


def main():
    parser = argparse.ArgumentParser(
        description="Scaffold a new robot for Joshua training.",
    )
    parser.add_argument(
        "--name", required=True,
        help="Robot name (snake_case, e.g. hexapod, quadruped)",
    )
    parser.add_argument(
        "--usd", default=None,
        help="USD filename (default: <name>_isaac.usda). "
             "Must be placed in simulation/models/.",
    )
    parser.add_argument(
        "--algorithm", default="rsl_rl", choices=["rsl_rl", "skrl"],
        help="RL algorithm (default: rsl_rl)",
    )
    args = parser.parse_args()

    robot_name = args.name.lower().replace("-", "_")
    usd_filename = args.usd or f"{robot_name}_isaac.usda"

    print(f"\nScaffolding new robot: {robot_name}")
    print(f"USD filename: {usd_filename}\n")

    config_dir = os.path.join(PRESET_DIR, robot_name)
    if os.path.exists(config_dir):
        print(f"Error: {os.path.relpath(config_dir, REPO_ROOT)} already exists.",
              file=sys.stderr)
        sys.exit(1)

    train_template = _read_template("_template_train.pbtxt")
    eval_template = _read_template("_template_eval.pbtxt")

    train_content = _substitute(train_template, robot_name, usd_filename)
    eval_content = _substitute(eval_template, robot_name, usd_filename)

    if args.algorithm != "rsl_rl":
        train_content = train_content.replace(
            'algorithm: "rsl_rl"', f'algorithm: "{args.algorithm}"'
        )
        eval_content = eval_content.replace(
            'algorithm: "rsl_rl"', f'algorithm: "{args.algorithm}"'
        )

    train_path = os.path.join(config_dir, f"{robot_name}_train_isaac.pbtxt")
    eval_path = os.path.join(config_dir, f"{robot_name}_eval_isaac.pbtxt")
    readme_dir = os.path.join(MODELS_DIR, robot_name)
    readme_path = os.path.join(readme_dir, "README.md")

    print("Generated files:")
    _write_file(train_path, train_content)
    _write_file(eval_path, eval_content)
    _write_file(readme_path, _generate_model_readme(robot_name, usd_filename))

    usd_path = os.path.join(MODELS_DIR, usd_filename)

    print(f"\n{'=' * 60}")
    print("Next steps:")
    print(f"{'=' * 60}")
    print(f"""
  1. Create your USD articulation:
     {os.path.relpath(usd_path, REPO_ROOT)}

     Requirements:
     - Articulation root on the main body (e.g. torso)
     - Z-up axis, meter units
     - Collision geometry on all bodies
     - Joints named descriptively (they appear in config)

  2. Edit the training preset:
     {os.path.relpath(train_path, REPO_ROOT)}

     Fill in the TODO markers:
     - init_joint_pos entries matching your USD joint names
     - Uncomment additional rewards/observations as needed
     - Tune action_scale for your actuators
     - Set min_root_height for termination

  3. Run training:
     bazel run //launcher:joshua_main -- \\
         --config {os.path.relpath(train_path, REPO_ROOT)}

  4. After training, edit the eval preset:
     {os.path.relpath(eval_path, REPO_ROOT)}

     - Copy your final task_config from the training preset
     - Set checkpoint_path to your saved model

  5. Run evaluation:
     bazel run //launcher:joshua_main -- \\
         --config {os.path.relpath(eval_path, REPO_ROOT)}
""")


if __name__ == "__main__":
    main()
