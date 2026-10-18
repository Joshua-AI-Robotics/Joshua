"""Validate a skill's YAML header and required string fields."""

import re
import sys
from pathlib import Path

import yaml


def check(path):
    text = path.read_text(encoding="utf-8")
    header = re.match(r"\A---\n(.*?)\n---(?:\n|$)", text, flags=re.S)
    if not header:
        raise ValueError("expected a YAML header between --- lines")
    metadata = yaml.safe_load(header.group(1))
    if not isinstance(metadata, dict):
        raise ValueError("metadata must be a YAML mapping")
    for key in ("name", "description"):
        value = metadata.get(key)
        if not isinstance(value, str) or not value.strip():
            raise ValueError(f"{key} must be a nonempty string")
    if metadata["name"] != path.parent.name:
        raise ValueError("name must match the skill directory")


if __name__ == "__main__":
    skill_path = Path(sys.argv[1])
    try:
        check(skill_path)
    except (OSError, UnicodeError, ValueError, yaml.YAMLError) as error:
        print(f"BAD FRONTMATTER: {skill_path}: {error}", file=sys.stderr)
        sys.exit(1)
