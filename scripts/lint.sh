#!/usr/bin/env bash
set -euo pipefail

# Ensure ~/.local/bin is available (common location for --user installs)
if ! command -v pre-commit >/dev/null 2>&1; then
  if [ -x "$HOME/.local/bin/pre-commit" ]; then
    export PATH="$HOME/.local/bin:$PATH"
  fi
fi

if command -v pre-commit >/dev/null 2>&1; then
  PRECOMMIT="pre-commit"
elif python3 -c "import pre_commit" >/dev/null 2>&1; then
  PRECOMMIT="python3 -m pre_commit"
else
  echo "pre-commit not found. Install it with: python3 -m pip install --user pre-commit" >&2
  exit 1
fi

# Helper to run a specific hook and collect failures without stopping early
fail=0
run_hook() {
  if ! "$PRECOMMIT" run "$1" --all-files "${@:2}"; then
    fail=1
  fi
}

mode="check"
if [ "${1:-}" = "--fix" ]; then
  mode="fix"
fi

if [ "$mode" = "fix" ]; then
  # Auto-fix using manual-stage hooks
  run_hook trailing-whitespace --hook-stage manual
  run_hook end-of-file-fixer --hook-stage manual

  # clang-format in-place on tracked + untracked (non-ignored) C/C++/Proto files
  if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format not found. Install it and retry (e.g., sudo apt install clang-format)." >&2
    exit 1
  fi

  mapfile -d '' CF_FILES < <( (
    git ls-files -z -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' '*.proto'; \
    git ls-files --others --exclude-standard -z -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' '*.proto' \
  ) | sort -zu )

  if [ ${#CF_FILES[@]} -gt 0 ]; then
    clang-format -i "${CF_FILES[@]}" || fail=1
  fi

  # Validate formatting after fixes (check-only)
  "$PRECOMMIT" run clang-format-check --all-files --hook-stage pre-push || fail=1

  if [ "$fail" -ne 0 ]; then
    echo "Fixes applied or checks failed. Stage changes and rerun if needed."
    exit 1
  fi
  echo "Linting fixes applied and validated."
  exit 0
fi

# -------- check-only path (no file modifications) --------

# 1) YAML validation (does not modify files)
echo "Checking YAML files..."
"$PRECOMMIT" run check-yaml --all-files --hook-stage pre-push || fail=1

# 2) clang-format check-only
echo "Checking C/C++/Proto formatting..."
"$PRECOMMIT" run clang-format-check --all-files --hook-stage pre-push || fail=1

# 3) trailing whitespace and EOF newline checks (custom check-only, no modifications)

# Build candidate file list: tracked + untracked (excluding ignored)
mapfile -d '' FILES < <( (
  git ls-files -z; \
  git ls-files --others --exclude-standard -z \
) | sort -zu )

tw_issues=0
eof_issues=0

for f in "${FILES[@]}"; do
  # Skip directories and non-existent
  [ -f "$f" ] || continue
  # Skip generated/build and external
  case "$f" in
    bazel-*/*|external/*) continue ;;
  esac
  # Only check text files
  if ! grep -Iq . "$f" 2>/dev/null; then
    continue
  fi
  # trailing whitespace check
  if grep -nP "\s+$" "$f" >/dev/null 2>&1; then
    echo "trailing whitespace: $f"
    tw_issues=1
  fi
  # EOF newline check (only if non-empty)
  if [ -s "$f" ]; then
    last_char=$(tail -c 1 "$f" 2>/dev/null || true)
    # If tail returns nothing (shouldn't for non-empty), skip
    if [ -n "$last_char" ]; then
      if [ "$last_char" != $'\n' ]; then
        echo "missing newline at EOF: $f"
        eof_issues=1
      fi
    fi
  fi
done

if [ "$fail" -ne 0 ] || [ "$tw_issues" -ne 0 ] || [ "$eof_issues" -ne 0 ]; then
  echo "Check-only failed. Run with --fix to auto-correct, then stage and rerun."
  exit 1
fi

echo "Check-only passed."
