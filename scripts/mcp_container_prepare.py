#!/usr/bin/env python3
"""Build/package MCP in the CPU-only preparation container, never on the host."""

import argparse
import shutil
import stat
import subprocess
import sys
from pathlib import Path

BINARIES = (
    "tools/mcp/config_validation_server",
    "tools/mcp/setup_smoke",
    "config/joshua_validate_config",
    "config/joshua_inspect_config",
    "tools/mcp/image_observer",
)


def copy_tree(source, destination, allowed_roots, ancestors=()):
    """Dereference Bazel runfiles while refusing cycles, escapes and special files."""
    source = Path(source)
    try:
        resolved = source.resolve(strict=True)
    except (OSError, RuntimeError) as error:
        raise RuntimeError(
            f"Cannot resolve runfiles entry (possible cycle): {source}"
        ) from error
    if not any(resolved == root or root in resolved.parents for root in allowed_roots):
        raise RuntimeError(
            f"Runfiles entry resolves outside allowed roots: {source} -> {resolved}"
        )
    mode = resolved.stat().st_mode
    if stat.S_ISDIR(mode):
        if resolved in ancestors:
            raise RuntimeError(f"Runfiles directory cycle: {source}")
        destination.mkdir(parents=True, exist_ok=True)
        for entry in sorted(resolved.iterdir()):
            copy_tree(
                entry, destination / entry.name, allowed_roots, (*ancestors, resolved)
            )
        shutil.copystat(resolved, destination)
    elif stat.S_ISREG(mode):
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(resolved, destination)
    else:
        raise RuntimeError(f"Runfiles entry is not a regular file/directory: {source}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stack", required=True, choices=("u22", "u24"))
    args = parser.parse_args()
    output = Path("/artifacts")
    flags = [
        f"--config={args.stack}",
        "--config=x86-base",
        "--@rules_python//python/config_settings:python_version="
        + ("3.10" if args.stack == "u22" else "3.12"),
    ]
    targets = [
        "//" + name.rsplit("/", 1)[0] + ":" + name.rsplit("/", 1)[1]
        for name in BINARIES
    ]
    subprocess.run(["bazel", "build", *flags, *targets], check=True, stdout=sys.stderr)
    output_base = Path(
        subprocess.check_output(
            ["bazel", "info", *flags, "output_base"], text=True
        ).strip()
    ).resolve()
    bazel_bin = Path(
        subprocess.check_output(
            ["bazel", "info", *flags, "bazel-bin"], text=True
        ).strip()
    ).resolve()
    allowed = [Path("/workspace").resolve(), output_base]
    for name in BINARIES:
        executable = bazel_bin / name
        copy_tree(executable, output / name, allowed)
        runfiles = executable.with_name(executable.name + ".runfiles")
        if runfiles.is_dir():
            copy_tree(runfiles, output / (name + ".runfiles"), allowed)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
