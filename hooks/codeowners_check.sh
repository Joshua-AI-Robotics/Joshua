#!/usr/bin/env bash
# Verify that every top-level directory has an entry in .github/CODEOWNERS.
#
# An unowned directory is invisible: GitHub requests no reviewer for it, and
# under "Require review from Code Owners" it is the one path anybody can merge
# unreviewed. Adding a top-level directory is exactly when that gap gets
# introduced and never noticed, so this fails the build instead.
#
# Also flags entries pointing at directories that no longer exist, which are
# dead rules that quietly stop protecting anything after a rename.
#
# Only TOP-LEVEL directories are required. Subdirectory rules are optional and
# exist to hand a subsystem to a specific owner.
#
# Usage: hooks/codeowners_check.sh
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required by $0" >&2
  exit 1
fi

python3 - <<'PY'
import re
import subprocess
import sys

CODEOWNERS = ".github/CODEOWNERS"

try:
    tracked = subprocess.check_output(["git", "ls-files"], text=True).splitlines()
except subprocess.CalledProcessError:
    print("codeowners check: not a git repository", file=sys.stderr)
    sys.exit(1)

dirs = sorted({f.split("/")[0] for f in tracked if "/" in f})

try:
    with open(CODEOWNERS, encoding="utf-8") as fh:
        lines = fh.readlines()
except FileNotFoundError:
    print(f"codeowners check: {CODEOWNERS} is missing", file=sys.stderr)
    sys.exit(1)

# A rule is `<pattern> <owner>...`. Strip comments and blank lines, then keep
# the top-level directory rules: a leading slash, one path segment, a trailing
# slash, and at least one owner.
owned = set()
unowned_rules = []
for raw in lines:
    line = raw.split("#", 1)[0].strip()
    if not line:
        continue
    parts = line.split()
    pattern = parts[0]
    if len(parts) == 1:
        unowned_rules.append(pattern)
        continue
    m = re.fullmatch(r"/([^/]+)/", pattern)
    if m:
        owned.add(m.group(1))

missing = [d for d in dirs if d not in owned]
stale = sorted(d for d in owned if d not in dirs)

failed = False

if missing:
    failed = True
    print(f"{CODEOWNERS}: top-level directories with no owner:", file=sys.stderr)
    for d in missing:
        print(f"  /{d}/", file=sys.stderr)
    print(
        "\nAdd a line of the form `/<dir>/    @owner` (see the file's header "
        "for the ordering rules).",
        file=sys.stderr,
    )

if stale:
    failed = True
    print(f"\n{CODEOWNERS}: rules for directories that do not exist:", file=sys.stderr)
    for d in stale:
        print(f"  /{d}/", file=sys.stderr)

if unowned_rules:
    failed = True
    print(f"\n{CODEOWNERS}: patterns with no owner listed:", file=sys.stderr)
    for p in unowned_rules:
        print(f"  {p}", file=sys.stderr)

if failed:
    sys.exit(1)

print(f"codeowners OK: all {len(dirs)} top-level directories have an owner")
PY
