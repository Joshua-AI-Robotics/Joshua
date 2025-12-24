# This script is used to build the Joshua Project targets in the Docker environment for cross-compilation and multi-arch support.
# Example usage: ./scripts/build.py //launcher:joshua_main_pkg --os=u22 --cpu=arm64

#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Build Joshua Project targets in Docker environment",
        epilog="Example: %(prog)s //launcher:joshua_main_pkg --os=u24 --cpu=arm64",
    )

    parser.add_argument(
        "--os",
        choices=["u22", "u24"],
        default="u22",
        help="Target OS (u22=Ubuntu 22.04/Humble, u24=Ubuntu 24.04/Jazzy)",
    )
    parser.add_argument(
        "--cpu",
        choices=["x86", "amd64", "arm64", "aarch64"],
        default="x86",
        help="Target CPU architecture",
    )

    args, bazel_args = parser.parse_known_args()

    if not bazel_args:
        print("❌ Error: No bazel target specified.")
        sys.exit(1)

    # Normalize CPU
    cpu_map = {"x86": "x86", "amd64": "x86", "arm64": "arm64", "aarch64": "arm64"}
    target_cpu = cpu_map[args.cpu.lower()]
    target_os = args.os

    # Determine Docker Service and Bazel Configs
    # On x86 host, building for arm64 will use the arm64 container via QEMU (Emulation).
    service_suffix = "-arm64" if target_cpu == "arm64" else ""
    
    # Select Bazel config based on target architecture
    # Note: We use 'arm64-base' for emulation builds, not 'arm64-ros2' which requires cross-compilation sysroot
    config_cpu = "arm64-base" if target_cpu == "arm64" else "x86-base"
    
    bazel_configs = [f"--config={target_os}", f"--config={config_cpu}"]
    service_name = f"joshua-{target_os}{service_suffix}"
    
    # Construct Bazel flags
    bazel_flags = " ".join(bazel_configs + bazel_args)

    print(f"🚀 Container Service: {service_name}")
    print(f"🎯 Target: {bazel_flags}")
    if target_cpu == "arm64":
        print("🐢 Using Emulation Strategy (Running on QEMU/ARM64 container)")

    # Ensure container_build.sh is executable
    if os.path.exists("scripts/container_build.sh"):
         # Make script executable by everyone (755) to ensure container user can run it
         os.chmod("scripts/container_build.sh", 0o755)

    # Docker Command
    # We mount the script and run it inside the container
    docker_cmd = [
        "docker",
        "compose",
        "run",
        "--rm",
        service_name,
        "/workspace/scripts/container_build.sh",
        target_os,
        target_cpu,
        bazel_args[0], # Pass the target label separately for cquery
        *bazel_configs, # Pass configs
    ]
    # Add remaining bazel args (like --compilation_mode=dbg)
    docker_cmd.extend(bazel_args[1:])

    try:
        subprocess.check_call(docker_cmd)
        print("-" * 60)
        print(f"✨ Done! Check 'dist/{target_os}/{target_cpu}' for your build artifacts.")

    except subprocess.CalledProcessError as e:
        print(f"❌ Build failed with exit code {e.returncode}")
        sys.exit(e.returncode)
    except KeyboardInterrupt:
        print("\n⚠️ Build interrupted")
        sys.exit(130)


if __name__ == "__main__":
    main()
