#!/usr/bin/env bash
# Verify the repo-owned skill library under docs/skills/ is well formed.
#
# docs/skills/ is the single source of truth for Joshua's skills;
# scripts/install-skills.sh links them into each agent's real skill directory
# (see docs/skills/README.md). A malformed or unindexed skill therefore rots
# silently — it installs, but an agent that loads it gets broken frontmatter or
# a dead reference, and nothing else catches it. This guard is blocking in CI
# for the same reason hooks/agents_doc_check.sh is: stale routing an agent will
# follow is worse than none. See docs/AGENTS_RFC.md §5.
#
# Checks, all blocking:
#   1. every docs/skills/<name>/ has a SKILL.md with `name:` and `description:`
#      frontmatter, and `name:` matches the directory name;
#   2. every skill directory is listed in docs/skills/README.md;
#   3. Markdown links and @imports *inside* each SKILL.md resolve.
#
# Usage: hooks/skills_doc_check.sh
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SKILLS_DIR="docs/skills"
INDEX="$SKILLS_DIR/README.md"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required by $0" >&2
  exit 1
fi

# No skill library yet is not an error — the guard is a no-op until docs/skills/
# exists, so it can land before the first skill does.
if [[ ! -d "$SKILLS_DIR" ]]; then
  echo "no $SKILLS_DIR/ yet — skill check skipped"
  exit 0
fi

if ! python3 -c 'import yaml' >/dev/null 2>&1; then
  echo "PyYAML is required; install it with: python3 -m pip install PyYAML==6.0.2" >&2
  exit 1
fi

if [[ ! -f "$INDEX" ]]; then
  echo "MISSING: $INDEX — the skill index must exist once $SKILLS_DIR/ does" >&2
  exit 1
fi

# Strip Markdown code fences, spans, and indented blocks before scanning a file
# for links/imports — an example link in a code block is documentation, not a
# live reference. Same reasoning as hooks/agents_doc_check.sh.
strip_code() {
  python3 -c '
import re, sys

text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
text = re.sub(r"<!--.*?-->", " ", text, flags=re.S)

lines = text.split("\n")
out, fence = [], None
for line in lines:
    stripped = line.lstrip()
    marker = re.match(r"(`{3,}|~{3,})", stripped)
    if fence is None and marker:
        fence = marker.group(1)[0]
        continue
    if fence is not None:
        if marker and marker.group(1)[0] == fence:
            fence = None
        continue
    if line.startswith("    ") or line.startswith("\t"):
        continue
    out.append(line)

print(re.sub(r"(`+)(?:(?!\1).)*?\1", " ", "\n".join(out)))
' "$1"
}

status=0

# Discover skill directories. This MUST match the discovery rule in
# scripts/install-skills.sh (joshua-* only) — otherwise a skill can pass CI here
# yet never be installed, or a non-skill helper dir (e.g. shared assets) can
# fail CI for lacking a SKILL.md.
mapfile -t SKILL_DIRS < <(find "$SKILLS_DIR" -mindepth 1 -maxdepth 1 -type d -name 'joshua-*' | sort)

# Catch a SKILL.md in a directory that does NOT match joshua-* — a typo like
# joshua_add_foo/ or a missing prefix (add-foo/). Discovery above would skip it
# and the installer would never install it, so the "source of truth" would
# silently hold a non-invocable skill. Fail loudly instead.
while IFS= read -r stray; do
  [[ -z "$stray" ]] && continue
  d="$(dirname "$stray")"
  echo "MISNAMED SKILL: $stray is in $(basename "$d")/, which is not a joshua-* dir" >&2
  echo "  Rename the directory to joshua-<name>/ so it is discovered and installed." >&2
  status=1
done < <(find "$SKILLS_DIR" -mindepth 2 -maxdepth 2 -name SKILL.md \
           -not -path "$SKILLS_DIR/joshua-*/SKILL.md")

# Catch a skill authored as a flat file (docs/skills/joshua-x.md) instead of a
# directory. Both the guard and the installer key off directories, so a flat
# file would pass silently and never install. README.md is the one allowed
# top-level file.
while IFS= read -r stray; do
  [[ -z "$stray" ]] && continue
  echo "STRAY FILE: $stray — skills are directories (joshua-<name>/SKILL.md), not flat files" >&2
  status=1
done < <(find "$SKILLS_DIR" -mindepth 1 -maxdepth 1 -type f -name '*.md' -not -name README.md)

index_text="$(strip_code "$INDEX")"

for dir in "${SKILL_DIRS[@]}"; do
  name="$(basename "$dir")"
  skill="$dir/SKILL.md"

  if [[ ! -f "$skill" ]]; then
    echo "MISSING SKILL.md: $dir/ has no SKILL.md" >&2
    status=1
    continue
  fi

  # 1. Parse YAML, then validate the required fields and directory name.
  if ! python3 "$ROOT/hooks/skill_metadata_check.py" "$skill"; then
    status=1
  fi

  # 2. listed in the index — require a link to the skill, not a bare mention, so
  # a deleted table row cannot be masked by the name appearing in prose, and
  # `joshua-build` is not satisfied by `joshua-build-fast`.
  if ! grep -qF "($name/SKILL.md" <<<"$index_text"; then
    echo "UNINDEXED: $name/ has no link in $INDEX (expected a '($name/SKILL.md)' link)" >&2
    status=1
  fi

  # 3. links and @imports inside SKILL.md resolve, relative to the skill dir.
  while IFS= read -r target; do
    [[ -z "$target" ]] && continue
    # Drop an optional Markdown link title: [x](path "title") -> path.
    target="${target%% *}"
    case "$target" in
      http://*|https://*|mailto:*|'#'*) continue ;;
    esac
    path="${target%%#*}"
    [[ -z "$path" ]] && continue
    if [[ ! -e "$dir/$path" ]]; then
      echo "BROKEN: $skill -> $target" >&2
      status=1
    fi
  done < <(strip_code "$skill" | grep -oE '\]\([^)]+\)' | sed -E 's/^\]\((.*)\)$/\1/')

  while IFS= read -r target; do
    [[ -z "$target" ]] && continue
    case "$target" in
      '~'*|/*) continue ;;
    esac
    if [[ ! -e "$dir/$target" ]]; then
      echo "BROKEN IMPORT: $skill -> @$target" >&2
      status=1
    fi
  done < <(strip_code "$skill" |
           grep -oE '(^|[[:space:]])@[^[:space:]]+\.md' |
           sed -E 's/^[[:space:]]*@//')
done

# The per-skill loop asserts every skill has an index link. Also check the
# inverse: every skill link in the index points at a skill that still exists,
# so a deleted skill whose README row was left behind is caught too.
while IFS= read -r target; do
  [[ -z "$target" ]] && continue
  target="${target%% *}"
  path="${target%%#*}"
  [[ -z "$path" ]] && continue
  if [[ ! -e "$SKILLS_DIR/$path" ]]; then
    echo "DANGLING INDEX LINK: $INDEX -> $target (no such skill)" >&2
    status=1
  fi
done < <(strip_code "$INDEX" | grep -oE '\]\(joshua-[^)]+\)' | sed -E 's/^\]\((.*)\)$/\1/')

if [[ $status -eq 0 ]]; then
  echo "skills OK: every skill has valid frontmatter, is indexed, and its links resolve"
else
  echo "" >&2
  echo "Fix the problems above. See docs/skills/README.md for the skill contract." >&2
fi

exit $status
