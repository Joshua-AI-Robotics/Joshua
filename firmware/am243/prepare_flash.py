"""Prepare an LP-AM243 HS-FS OSPI configuration without accessing hardware."""

import argparse
import hashlib
import shlex
import sys
from pathlib import Path

ARTIFACTS = {
    "uart": "am243_dual_transport_rev4.release.appimage.hs_fs",
    "jw": "am243_ethercat_jw.release.appimage.hs_fs",
    "jw-soes": "am243_ethercat_jw_soes.release.appimage.hs_fs",
}


def prepare(sdk, image_dir, profile, operation, output, port):
    sdk, image_dir, output = (
        Path(sdk).resolve(),
        Path(image_dir).resolve(),
        Path(output),
    )
    boot = sdk / "mcu_plus_sdk/tools/boot"
    writer = boot / "sbl_prebuilt/am243x-lp/sbl_uart_uniflash.release.hs_fs.tiimage"
    loader = boot / "sbl_prebuilt/am243x-lp/sbl_ospi.release.hs_fs.tiimage"
    application = image_dir / ARTIFACTS[profile]
    tool = boot / "uart_uniflash.py"
    if operation not in ("flash", "flashverify"):
        raise ValueError("Operation must be flash or flashverify")
    if not port or "\n" in port or "\r" in port or "\0" in port:
        raise ValueError("Supply one explicit serial port")
    for path in (writer, loader, application, tool):
        if any(c.isspace() or c in "#\"'" for c in str(path)):
            raise ValueError(
                "TI configuration input paths must contain no whitespace or quotes"
            )
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Missing or empty input: {path}")
    # Offsets match the SDK 09 LP-AM243 HS-FS configuration already used here.
    lines = [f"--flash-writer={writer}"]
    if operation == "flash":
        lines.append("--operation=flash-phy-tuning-data")
    lines.extend(
        [
            f"--file={loader} --operation={operation} --flash-offset=0x0",
            f"--file={application} --operation={operation} --flash-offset=0x80000",
        ]
    )
    digest = hashlib.sha256(application.read_bytes()).hexdigest()
    with output.open("x", encoding="utf-8") as stream:
        stream.write("\n".join(lines) + "\n")
    command = shlex.join(
        ["python3", str(tool), "-p", port, "--cfg", str(output.resolve())]
    )
    return application, digest, command


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--sdk", type=Path, required=True, help="Industrial SDK 09.00.00.03 root"
    )
    parser.add_argument(
        "--image-dir",
        type=Path,
        default=Path(__file__).parent / "joshua_dual_transport/out",
    )
    parser.add_argument("--profile", choices=ARTIFACTS, required=True)
    parser.add_argument("--operation", choices=("flash", "flashverify"), required=True)
    parser.add_argument(
        "--output", type=Path, required=True, help="New config; never overwritten"
    )
    parser.add_argument(
        "--port", required=True, help="Explicit port; not opened by this command"
    )
    args = parser.parse_args(argv)
    try:
        image, digest, command = prepare(
            args.sdk,
            args.image_dir,
            args.profile,
            args.operation,
            args.output,
            args.port,
        )
    except (OSError, ValueError) as error:
        parser.exit(2, f"{error}\n")
    print(f"Prepared: {args.output.resolve()}\nImage: {image}\nSHA256: {digest}")
    print(f"Review and run separately ({args.operation}; not executed):\n{command}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
