#!/usr/bin/env python3
"""Give one USB ALSA card a persistent name using its USB identity, not its index."""

import argparse
import os
import re
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "card", help="Current card name, e.g. card1 from /proc/asound/cards"
    )
    parser.add_argument("name", help="Persistent ALSA ID, e.g. JoshuaMic")
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("Run with sudo")
    if not re.fullmatch(r"card[0-9]+", args.card) or not re.fullmatch(
        r"[A-Za-z][A-Za-z0-9_]{0,14}", args.name
    ):
        parser.error("Invalid card or ALSA ID")
    card = Path("/sys/class/sound", args.card)
    usb = next((p for p in card.resolve().parents if (p / "idVendor").exists()), None)
    if usb is None:
        parser.error("Card has no USB identity")
    identity = {
        key: (usb / key).read_text().strip()
        for key in ("idVendor", "idProduct", "serial")
    }
    if any(not re.fullmatch(r"[A-Za-z0-9_. -]+", value) for value in identity.values()):
        parser.error("USB identity missing or requires unsupported udev escaping")
    for other in Path("/sys/class/sound").glob("card[0-9]*"):
        if other != card and (other / "id").read_text().strip() == args.name:
            parser.error("ALSA ID is already in use")
        if other != card:
            for parent in other.resolve().parents:
                if (parent / "idVendor").exists():
                    if all(
                        (parent / key).exists()
                        and (parent / key).read_text().strip() == value
                        for key, value in identity.items()
                    ):
                        parser.error(
                            "Multiple cards have this USB identity; "
                            "use physical-port matching instead"
                        )
                    break
    matches = ", ".join(f'ATTRS{{{key}}}=="{value}"' for key, value in identity.items())
    rule = (
        'ACTION=="add|change", SUBSYSTEM=="sound", KERNEL=="card[0-9]*", '
        f'{matches}, ATTR{{id}}="{args.name}"\n'
    )
    path = Path("/etc/udev/rules.d", f"90-joshua-{args.name}.rules")
    if path.exists() and path.read_text() != rule:
        parser.error(f"Existing rule differs: {path}; review it before replacing")
    path.write_text(rule)
    path.chmod(0o644)
    subprocess.run(["udevadm", "control", "--reload-rules"], check=True)
    subprocess.run(["udevadm", "trigger", "--action=change", str(card)], check=True)
    subprocess.run(["udevadm", "settle", "--timeout=10"], check=True)
    if (card / "id").read_text().strip() != args.name:
        raise RuntimeError("ALSA ID did not change; inspect udev logs")
    print(f'Use alsa_audio_config.device: "plughw:CARD={args.name},DEV=0"')


if __name__ == "__main__":
    main()
