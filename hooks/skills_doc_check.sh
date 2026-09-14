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

# Read one frontmatter key from a SKILL.md (empty if absent/malformed).
frontmatter_value() {
  python3 -c '
import re, sys
text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
m = re.match(r"^---\n(.*?)\n---\n", text, flags=re.S)
if not m:
    sys.exit(0)
for line in m.group(1).split("\n"):
    k, _, v = line.partition(":")
    if k.strip() == sys.argv[2]:
        print(v.strip())
        break
' "$1" "$2"
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

if [[ ${#SKILL_DIRS[@]} -eq 0 ]]; then
  if [[ $status -eq 0 ]]; then
    echo "no skills in $SKILLS_DIR/ yet — index present, nothing to check"
  fi
  exit $status
fi

index_text="$(strip_code "$INDEX")"

for dir in "${SKILL_DIRS[@]}"; do
  name="$(basename "$dir")"
  skill="$dir/SKILL.md"

  if [[ ! -f "$skill" ]]; then
    echo "MISSING SKILL.md: $dir/ has no SKILL.md" >&2
    status=1
    continue
  fi

  # 1. frontmatter present and consistent with the directory name.
  fm_name="$(frontmatter_value "$skill" name)"
  fm_desc="$(frontmatter_value "$skill" description)"
  if [[ -z "$fm_name" ]]; then
    echo "BAD FRONTMATTER: $skill has no 'name:' key" >&2
    status=1
  elif [[ "$fm_name" != "$name" ]]; then
    echo "NAME MISMATCH: $skill declares name: $fm_name but lives in $name/" >&2
    status=1
  fi
  if [[ -z "$fm_desc" ]]; then
    echo "BAD FRONTMATTER: $skill has no 'description:' key" >&2
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

if [[ $status -eq 0 ]]; then
  echo "skills OK: every skill has valid frontmatter, is indexed, and its links resolve"
else
  echo "" >&2
  echo "Fix the problems above. See docs/skills/README.md for the skill contract." >&2
fi

exit $status
