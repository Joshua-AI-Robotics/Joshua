#!/usr/bin/env python3
"""Build-only firmware and hardware-free Docker soak; never launch or flash."""

import argparse
import hashlib
import json
import os
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGETS = (
    "//robot/board/joshua_wire:mixed_binding_test",
    "//robot/board/joshua_wire:joshua_wire_coordinator_test",
    "//robot/comm/serial:serial_exchange_test",
    "//robot/comm/serial:serial_io_owner_test",
    "//robot/comm/serial:joshua_wire_frame_stream_test",
    "//firmware/common:joshua_mixed_device_test",
)


def capture(args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_manifest():
    files = (
        subprocess.check_output(
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
            cwd=ROOT,
        )
        .decode()
        .split("\0")
    )
    hashes = {
        name: digest(ROOT / name)
        for name in sorted(set(files))
        if name and (ROOT / name).is_file()
    }
    return {
        "commit": capture(["git", "rev-parse", "HEAD"]),
        "status": capture(["git", "status", "--porcelain"]),
        "files": hashes,
        "tree_sha256": hashlib.sha256(
            json.dumps(hashes, sort_keys=True).encode()
        ).hexdigest(),
    }


def run_step(report_dir, label, service, shell):
    command = [
        "docker",
        "compose",
        "run",
        "--rm",
        "-T",
        "-v",
        f"{report_dir}:/qualification",
        service,
        "bash",
        "-lc",
        shell,
    ]
    started = time.monotonic()
    with (report_dir / f"{label}.log").open("x") as log:
        result = subprocess.run(
            command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=False
        )
    return {
        "step": label,
        "command": command,
        "returncode": result.returncode,
        "duration_seconds": time.monotonic() - started,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="New evidence directory outside the repository",
    )
    parser.add_argument(
        "--runs", type=int, default=10, choices=range(1, 101), metavar="1..100"
    )
    parser.add_argument(
        "--skip-firmware-build",
        action="store_true",
        help="Run software soak only; artifact evidence remains absent",
    )
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents or ":" in str(output):
        parser.error("Output must be outside the repository and contain no colon")
    output.mkdir(parents=True, exist_ok=False)
    report = {
        "scope": "software_only",
        "physical_qualification": "not_run",
        "runs_per_target_per_stack": args.runs,
        "source": source_manifest(),
        "steps": [],
    }
    compose = json.loads(
        capture(
            ["docker", "compose", "--profile", "tasks", "config", "--format", "json"]
        )
    )
    report["images"] = {}
    for stack in ("u22", "u24"):
        image = compose["services"][f"test-{stack}"]["image"]
        report["images"][stack] = {
            "name": image,
            "id": capture(["docker", "image", "inspect", "--format", "{{.Id}}", image]),
        }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    try:
        if not args.skip_firmware_build:
            build = """set -euo pipefail
python3 -m venv /tmp/serial-pio
/tmp/serial-pio/bin/pip -q install platformio==6.1.18
/tmp/serial-pio/bin/pio --version > /qualification/platformio-version.txt
/tmp/serial-pio/bin/pio run -d firmware/teensy/41 -e teensy41-mixed-scalar
compiler="$HOME/.platformio/packages/toolchain-gccarmnoneeabi-teensy"
"$compiler/bin/arm-none-eabi-g++" --version > /qualification/compiler-version.txt
/tmp/serial-pio/bin/pio pkg list -d firmware/teensy/41 \\
  -e teensy41-mixed-scalar > /qualification/platformio-packages.txt
cp firmware/teensy/41/.pio/build/teensy41-mixed-scalar/firmware.hex /qualification/
cp firmware/teensy/41/.pio/build/teensy41-mixed-scalar/firmware.elf /qualification/
"""
            build += (
                f"chown -R {os.getuid()}:{os.getgid()} /qualification "
                "firmware/teensy/41/.pio\n"
            )
            report["steps"].append(
                run_step(output, "firmware-build", "test-u22", build)
            )
        for stack, python in (("u22", "3.10"), ("u24", "3.12")):
            # Serial execution also isolates repeated ROS graph fixtures. No
            # real preset, board path, launcher or upload command is accepted.
            xml_selector = (
                f"-path '*/run_*_of_{args.runs}/test.xml' "
                if args.runs > 1
                else "-maxdepth 1 "
            )
            shell = (
                "set -uo pipefail\n"
                f"bazel test --config={stack} --config=x86-base "
                f"--@rules_python//python/config_settings:python_version={python} "
                f"--runs_per_test={args.runs} --local_test_jobs=1 --test_output=errors "
                + " ".join(map(shlex.quote, TARGETS))
                + "\nresult=$?\n"
                f"mkdir -p /qualification/{stack}-xml\n"
                "find -L bazel-testlogs/robot/board/joshua_wire/mixed_binding_test "
                + xml_selector
                + "-name test.xml -exec cp --parents {} "
                f"/qualification/{stack}-xml/ \\;\n"
                f"chown -R {os.getuid()}:{os.getgid()} /qualification/{stack}-xml\n"
                "exit $result\n"
            )
            report["steps"].append(
                run_step(output, f"{stack}-soak", f"test-{stack}", shell)
            )
        cases = []
        report["mixed_xml_files"] = {
            stack: len(list(output.glob(f"{stack}-xml/**/*.xml")))
            for stack in ("u22", "u24")
        }
        for path in output.glob("*-xml/**/*.xml"):
            for case in ET.parse(path).iter("testcase"):
                cases.append(
                    {
                        "name": case.get("name"),
                        "seconds": case.get("time"),
                        "failed": case.find("failure") is not None,
                        "software_stop_ack_us": case.get("software_stop_ack_us"),
                        "properties": {
                            p.get("name"): p.get("value")
                            for p in case.findall("./properties/property")
                        },
                    }
                )
        report["mixed_cases"] = cases
        report["passed"] = (
            bool(cases)
            and all(n == args.runs for n in report["mixed_xml_files"].values())
            and all(s["returncode"] == 0 for s in report["steps"])
            and not any(c["failed"] for c in cases)
        )
    finally:
        report["evidence_sha256"] = {
            str(p.relative_to(output)): digest(p)
            for p in sorted(output.rglob("*"))
            if p.is_file() and p.name != "report.json"
        }
        report["source_unchanged_during_run"] = source_manifest() == report["source"]
        report["passed"] = (
            report.get("passed", False) and report["source_unchanged_during_run"]
        )
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(output / "report.json", flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
