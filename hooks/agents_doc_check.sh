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

FILES=(
  AGENTS.md
  CLAUDE.md
  GEMINI.md
  .github/copilot-instructions.md
  docs/AGENTS_RFC.md
)

status=0

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
  done < <(grep -oE '\]\([^)]+\)' "$f" | sed -E 's/^\]\((.*)\)$/\1/')
done

if [[ $status -eq 0 ]]; then
  echo "agent docs OK: all Markdown link targets resolve"
else
  echo "" >&2
  echo "Fix the links above, or update hooks/agents_doc_check.sh if a file moved." >&2
fi

exit $status
