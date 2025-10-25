#!/usr/bin/env python3
import sys


def main() -> int:
    # Example: block pushes if README is missing the Linting section
    try:
        with open("README.md", "r", encoding="utf-8") as f:
            content = f.read()
    except FileNotFoundError:
        print("README.md not found", file=sys.stderr)
        return 1

    if "Linting & Formatting" not in content:
        print("README is missing 'Linting & Formatting' section", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
