"""Check changed source files with the tools installed in the lint image."""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

C_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".proto"}
YAML_EXTENSIONS = {".yaml", ".yml"}
BUILD_NAMES = {"BUILD", "BUILD.bazel"}


def git(*args):
    return subprocess.check_output(["git", *args])


def file_kind(path):
    if path.parts[0] in {"ui", "external"} or path.parts[0].startswith("bazel-"):
        return None
    if path.name in BUILD_NAMES:
        return "build"
    if path.suffix in C_EXTENSIONS:
        return "cpp"
    if path.suffix in YAML_EXTENSIONS:
        return "yaml"
    if path.suffix == ".py":
        return "python"
    return None


def changed_files(base):
    if base is None:
        for candidate in ("origin/develop", "develop"):
            result = subprocess.run(
                ["git", "rev-parse", "--verify", candidate],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if result.returncode == 0:
                base = candidate
                break
        else:
            raise ValueError("Cannot find develop; fetch it or supply --base REF.")
    merge_base = git("merge-base", "HEAD", base).decode().strip()
    tracked = git("diff", "--name-only", "--no-renames", "-z", merge_base, "--")
    untracked = git("ls-files", "--others", "--exclude-standard", "-z")
    return sorted(
        {os.fsdecode(name) for name in (tracked + untracked).split(b"\0") if name}
    )


def run_tool(command, verbose):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode or verbose:
        sys.stderr.write(result.stdout.decode(errors="replace"))
    if result.returncode:
        print(f"Failed: {command[0]} ({command[-1]})", file=sys.stderr)
    return result.returncode == 0


def run_docker(paths, args):
    subprocess.run(["docker", "compose", "build", "lint"], check=True)
    command = [
        "docker",
        "compose",
        "run",
        "--rm",
        "-T",
        "--no-deps",
        "--user",
        f"{os.getuid()}:{os.getgid()}",
        "lint",
        "python3",
        "hooks/lint_check.py",
        "--files-from-stdin0",
    ]
    if args.fix:
        command.append("--fix")
    if args.verbose:
        command.append("--verbose")
    # An empty list must stay empty, rather than triggering Git discovery again.
    filenames = b"".join(os.fsencode(path) + b"\0" for path in paths)
    return subprocess.run(command, input=filenames).returncode


def lint_file(path, kind, fix, verbose):
    ok = True
    content = path.read_bytes()
    normalized = b"\n".join(line.rstrip(b" \t\r") for line in content.split(b"\n"))
    if normalized and not normalized.endswith(b"\n"):
        normalized += b"\n"
    if normalized != content:
        if fix:
            path.write_bytes(normalized)
        else:
            print(f"Whitespace or missing EOF newline: {path}", file=sys.stderr)
            ok = False
    # Absolute arguments cannot be mistaken for tool options, even for '-name.py'.
    filename = str(path.absolute())
    commands = []
    if kind == "cpp":
        commands.append(
            ["clang-format", *(["-i"] if fix else ["--dry-run", "--Werror"]), filename]
        )
    elif kind == "build":
        commands.append(["buildifier", f"-mode={'fix' if fix else 'check'}", filename])
    elif kind == "python":
        commands.extend(
            [
                [
                    "isort",
                    "--profile",
                    "black",
                    *([] if fix else ["--check-only"]),
                    filename,
                ],
                ["black", *([] if fix else ["--check"]), filename],
                ["flake8", "--max-line-length=88", "--extend-ignore=E203", filename],
            ]
        )
    for command in commands:
        if not run_tool(command, verbose):
            ok = False
    if kind == "yaml":
        import yaml

        try:
            with path.open(encoding="utf-8") as stream:
                # Consume every document. Composition checks syntax without
                # constructing objects, and accepts Compose's !reset/!override.
                for _ in yaml.compose_all(stream, Loader=yaml.SafeLoader):
                    pass
        except yaml.YAMLError as error:
            print(f"YAML syntax error in {path}: {error}", file=sys.stderr)
            ok = False
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true")
    parser.add_argument(
        "--docker",
        action="store_true",
        help="run tools in Docker after local Git discovery",
    )
    parser.add_argument(
        "--files-from-stdin0", action="store_true", help=argparse.SUPPRESS
    )
    parser.add_argument("--base", help="diff from the merge base with this Git ref/SHA")
    parser.add_argument("--verbose", "-v", dest="verbose", action="store_true")
    parser.add_argument("--quiet", "-q", dest="verbose", action="store_false")
    parser.set_defaults(verbose=False)
    parser.add_argument(
        "files", nargs="*", help="optional explicit files, relative to cwd"
    )
    args = parser.parse_args()
    try:
        if args.files_from_stdin0:
            if args.files or args.base or args.docker:
                raise ValueError(
                    "The stdin file list cannot be combined with Git discovery options."
                )
            root = Path.cwd()
            paths = [
                Path(os.fsdecode(name))
                for name in sys.stdin.buffer.read().split(b"\0")
                if name
            ]
            if any(path.is_absolute() or ".." in path.parts for path in paths):
                raise ValueError(
                    "The stdin file list must contain checkout-relative paths."
                )
        else:
            root = Path(os.fsdecode(git("rev-parse", "--show-toplevel")).strip())
            explicit = [Path(os.path.abspath(name)) for name in args.files]
            os.chdir(root)
            paths = (
                [path.relative_to(root) for path in explicit]
                if explicit
                else [Path(name) for name in changed_files(args.base)]
            )
        selected = []
        for path in paths:
            if not path.is_file() or path.is_symlink():
                continue
            kind = file_kind(path)
            if kind:
                selected.append((path, kind))

        if args.docker:
            return run_docker([path for path, _ in selected], args)

        required = set()
        for _, kind in selected:
            required.update(
                {
                    "cpp": ["clang-format"],
                    "python": ["isort", "black", "flake8"],
                    "build": ["buildifier"],
                    "yaml": [],
                }[kind]
            )
        missing = [name for name in sorted(required) if shutil.which(name) is None]
        if any(kind == "yaml" for _, kind in selected):
            try:
                import yaml  # noqa: F401
            except ImportError:
                missing.append("PyYAML")
        if missing:
            raise ValueError(
                f"Missing lint tools: {', '.join(missing)}. "
                "On the host, run ./hooks/lint_check.sh; "
                "inside a dev container, rebuild its image."
            )
        failed = False
        for path, kind in selected:
            if args.verbose:
                print(f"Checking {path}", flush=True)
            if not lint_file(path, kind, args.fix, args.verbose):
                failed = True
        if args.verbose:
            print(f"Checked {len(selected)} file(s).")
        return int(failed)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Lint check failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
