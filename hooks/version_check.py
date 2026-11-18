#!/usr/bin/env python3
"""Check the Bazel module version against the canonical VERSION file."""

import ast
import sys
from pathlib import Path


def check(root):
    expected = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not expected or len(expected.splitlines()) != 1:
        raise ValueError("VERSION must contain one nonempty version line")

    # Parse the shared Python/Starlark call syntax without executing MODULE.bazel.
    tree = ast.parse(
        (root / "MODULE.bazel").read_text(encoding="utf-8"), filename="MODULE.bazel"
    )
    modules = [
        statement.value
        for statement in tree.body
        if isinstance(statement, ast.Expr)
        and isinstance(statement.value, ast.Call)
        and isinstance(statement.value.func, ast.Name)
        and statement.value.func.id == "module"
    ]
    if len(modules) != 1:
        raise ValueError("MODULE.bazel must contain exactly one module() declaration")
    versions = [kw.value for kw in modules[0].keywords if kw.arg == "version"]
    if (
        len(versions) != 1
        or not isinstance(versions[0], ast.Constant)
        or not isinstance(versions[0].value, str)
    ):
        raise ValueError("MODULE.bazel module() must declare a literal version string")

    actual = versions[0].value
    if actual != expected:
        raise ValueError(
            f"VERSION is {expected!r}, but MODULE.bazel module() "
            f"version is {actual!r}. "
            f"Set module(version = {expected!r}, ...) to match VERSION."
        )
    print(f"Version consistency OK: VERSION and MODULE.bazel both declare {expected}")


if __name__ == "__main__":
    try:
        check(Path(__file__).resolve().parents[1])
    except (OSError, UnicodeError, SyntaxError, ValueError) as error:
        print(f"Version consistency check failed: {error}", file=sys.stderr)
        sys.exit(1)
