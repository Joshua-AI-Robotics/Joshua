#!/usr/bin/env python3
import argparse
import os
import shlex
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

    # Determine Docker Service Name
    # WE ALWAYS BUILD IN THE x86 CONTAINER (Cross-compilation)
    # This avoids QEMU emulation overhead.
    # joshua-u22 (x86) can build for u22-arm64
    # joshua-u24 (x86) can build for u24-arm64
    service_suffix = "-arm64" if target_cpu == "arm64" else ""
    service_name = f"joshua-{target_os}{service_suffix}"

    configs = f"--config={target_os} --config={target_cpu}"
    target_str = " ".join(bazel_args)

    # Ensure dist directory exists
    os.makedirs("dist", exist_ok=True)

    print(f"🚀 Building in container: {service_name}")

    # We construct a single bash command to run inside the container:
    # 1. Build the target
    # 2. Use cquery to find the output file path
    # 3. Copy that file to /workspace/dist/

    bash_cmd = f"""
    set -e
    echo "🔨 Building {target_str}..."
    bazel build {configs} {target_str}

    echo "🔍 Locating output artifacts..."
    # Get the output path of the target
    OUTPUT_PATH=$(bazel cquery {configs} --output=files {target_str} 2>/dev/null | head -n 1)

    echo "📄 Found artifact path: '$OUTPUT_PATH'"

    if [ -f "$OUTPUT_PATH" ]; then
        echo "📦 Copying $OUTPUT_PATH to /workspace/dist/..."
        mkdir -p /workspace/dist
        cp -f "$OUTPUT_PATH" /workspace/dist/

        # Fix permissions (since we are root in container)
        chown $(id -u):$(id -g) /workspace/dist/$(basename "$OUTPUT_PATH")

        echo "✅ Artifact saved to dist/$(basename "$OUTPUT_PATH")"
    else
        echo "⚠️  Could not locate single output file at '$OUTPUT_PATH'. Check bazel-bin inside container if needed."
        echo "    (Note: If this target produces multiple files, script might need update)"
    fi
    """

    docker_cmd = [
        "docker",
        "compose",
        "run",
        "--rm",
        service_name,
        "/bin/bash",
        "-c",
        bash_cmd,
    ]

    try:
        subprocess.check_call(docker_cmd)
        print("-" * 60)
        print(f"✨ Done! Check 'dist/' folder for your build artifacts.")

    except subprocess.CalledProcessError as e:
        print(f"❌ Build failed with exit code {e.returncode}")
        sys.exit(e.returncode)
    except KeyboardInterrupt:
        print("\n⚠️ Build interrupted")
        sys.exit(130)


if __name__ == "__main__":
    main()
