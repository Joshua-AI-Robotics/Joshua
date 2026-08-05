#!/usr/bin/env bash
# Verify that Markdown links in the agent instruction files still resolve.
#
# AGENTS.md routes agents to other docs. A renamed or deleted target turns it
# into a confidently wrong instruction file, which is worse than none — an
# agent will follow it. This guards against that.
#
# Scope is deliberately narrow: explicit Markdown link targets only. Paths and
# commands mentioned in prose or code blocks are NOT checked, because docs
# legitimately reference planned work (e.g. tools/flash/) and placeholders
# (e.g. <github-username>/<topic>). See docs/AGENTS_RFC.md.
#
# Usage: hooks/agents_doc_check.sh
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required by $0" >&2
  exit 1
fi

# Drop every Markdown code form before looking for imports. Claude Code's parser
# skips them, so an @import shown as an example is documentation, not a live
# import — and treating one as live fails CI on a correct doc.
#
# This is python3 rather than sed/awk because the forms do not fit line-based
# tools: fences open with ``` or ~~~, spans use runs of backticks that must be
# matched by an equal run, and indented blocks have no delimiter at all.
strip_code() {
  python3 -c '
import re, sys

text = open(sys.argv[1], encoding="utf-8", errors="replace").read()

# Claude Code strips block-level HTML comments before injecting the file, so an
# import inside one never runs. Every bridge here opens with a comment.
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
        continue                      # indented code block
    out.append(line)

# Runs of backticks close only on an equal-length run, so `` `x` `` is one span.
# Deliberately not re.S: a span must open and close on one line. An unmatched
# backtick in prose ("use ` for quoting") would otherwise swallow everything up
# to the next one, several paragraphs away, and hide a real import.
print(re.sub(r"(`+)(?:(?!\1).)*?\1", " ", "\n".join(out)))
' "$1"
}

# Bridges and the RFC are fixed and must exist.
REQUIRED=(
  AGENTS.md
  CLAUDE.md
  GEMINI.md
  .github/copilot-instructions.md
  docs/AGENTS_RFC.md
)

# Nested per-directory AGENTS.md files are discovered, not listed, so adding one
# (see docs/AGENTS_RFC.md §7) does not require editing this script.
discover_nested() {
  if git rev-parse --git-dir >/dev/null 2>&1; then
    # -co --exclude-standard also catches a nested file you have created but
    # not yet staged, so the check is useful before `git add`, not just in CI.
    git ls-files -co --exclude-standard -- '*/AGENTS.md'
  else
    find . -mindepth 2 -name AGENTS.md -not -path './.git/*' \
      -not -path './.venv*' -not -path './bazel-*' -printf '%P\n' 2>/dev/null
  fi
}

FILES=("${REQUIRED[@]}")
NESTED=()
while IFS= read -r nested; do
  [[ -n "$nested" ]] && FILES+=("$nested") && NESTED+=("$nested")
done < <(discover_nested)

status=0

# Claude Code reads CLAUDE.md, never AGENTS.md, at any level — and the root
# bridge does not cover subdirectories. Without a sibling bridge a nested file
# is invisible to it while other agents read it. See docs/AGENTS_RFC.md §7.5.
#
# Presence is not enough: an empty bridge, or one carrying its own rules
# instead of the import, passes a file-exists test while Claude Code still
# loads nothing. Require the import, and check the bridge's own links too.
for nested in "${NESTED[@]:+${NESTED[@]}}"; do
  bridge="$(dirname "$nested")/CLAUDE.md"
  if [[ ! -f "$bridge" ]]; then
    echo "MISSING BRIDGE: $nested has no $bridge" >&2
    echo "  Claude Code cannot see $nested without it. Create it with:" >&2
    echo "    printf '@AGENTS.md\\n' > $bridge" >&2
    status=1
    continue
  fi

  # `@./AGENTS.md` resolves to the same sibling file, so accept either spelling.
  if ! strip_code "$bridge" |
       grep -qE '(^|[[:space:]])@(\./)?AGENTS\.md([[:space:]]|$)'; then
    echo "INERT BRIDGE: $bridge does not import @AGENTS.md" >&2
    echo "  It must contain the import, or $nested never reaches Claude Code." >&2
    status=1
  fi

  FILES+=("$bridge")
done

for f in "${FILES[@]}"; do
  if [[ ! -f "$f" ]]; then
    echo "MISSING: $f is listed as an agent instruction file but does not exist" >&2
    status=1
    continue
  fi

  dir="$(dirname "$f")"

  # Extract ](target) pairs; drop external URLs, anchors, and mailto.
  while IFS= read -r target; do
    [[ -z "$target" ]] && continue
    case "$target" in
      http://*|https://*|mailto:*|'#'*) continue ;;
    esac

    # Strip any #anchor suffix before testing the path.
    path="${target%%#*}"
    [[ -z "$path" ]] && continue

    if [[ ! -e "$dir/$path" ]]; then
      echo "BROKEN: $f -> $target" >&2
      status=1
    fi
  done < <(strip_code "$f" | grep -oE '\]\([^)]+\)' | sed -E 's/^\]\((.*)\)$/\1/')

  # Claude Code @-imports resolve relative to the importing file, so a typo in
  # one is as silent as a broken link. Code spans are stripped because its own
  # parser skips them. Absolute and ~ imports are user-scope, not repo docs.
  while IFS= read -r target; do
    [[ -z "$target" ]] && continue
    case "$target" in
      '~'*|/*) continue ;;
    esac

    if [[ ! -e "$dir/$target" ]]; then
      echo "BROKEN IMPORT: $f -> @$target" >&2
      status=1
    fi
  done < <(strip_code "$f" |
           grep -oE '(^|[[:space:]])@[^[:space:]]+\.md' |
           sed -E 's/^[[:space:]]*@//')
done

# Gemini reads AGENTS.md only because .gemini/settings.json says to. The key
# moved from a flat `contextFileName` to a nested `context.fileName` in the
# 2025-09-17 settings migration, and a stale key fails silently — Gemini just
# falls back to GEMINI.md. Check the shape so that cannot recur unnoticed.
GEMINI_SETTINGS=".gemini/settings.json"
if [[ ! -f "$GEMINI_SETTINGS" ]]; then
  echo "MISSING: $GEMINI_SETTINGS — Gemini would not read AGENTS.md" >&2
  status=1
elif command -v python3 >/dev/null 2>&1; then
  if ! python3 -c '
import json, sys
with open(".gemini/settings.json") as fh:
    names = json.load(fh).get("context", {}).get("fileName", [])
sys.exit(0 if "AGENTS.md" in ([names] if isinstance(names, str) else names) else 1)
' 2>/dev/null; then
    echo "BAD CONFIG: $GEMINI_SETTINGS does not set context.fileName to include AGENTS.md" >&2
    echo '  Expected: {"context": {"fileName": ["AGENTS.md"]}}' >&2
    echo "  A flat \"contextFileName\" key is the pre-migration spelling and is ignored." >&2
    status=1
  fi
fi

if [[ $status -eq 0 ]]; then
  echo "agent docs OK: link targets resolve and every nested AGENTS.md is bridged"
else
  echo "" >&2
  echo "Fix the problems above, or update hooks/agents_doc_check.sh if a file moved." >&2
fi

exit $status
