#!/usr/bin/env bash
set -euo pipefail

FILES=("$@")
if [ ${#FILES[@]} -eq 0 ]; then
  exit 0
fi

fail=0
for f in "${FILES[@]}"; do
  # Skip non-existent
  [ -f "$f" ] || continue
  if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
    echo "clang-format issues in $f (run ./scripts/lint.sh --fix to update)" >&2
    fail=1
  fi
done

exit $fail

