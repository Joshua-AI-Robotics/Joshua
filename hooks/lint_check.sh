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

# Discover Python tooling if available
if command -v black >/dev/null 2>&1; then
  PY_BLACK="black"
elif python3 -c "import black" >/dev/null 2>&1; then
  PY_BLACK="python3 -m black"
else
  PY_BLACK=""
fi

if command -v flake8 >/dev/null 2>&1; then
  PY_FLAKE8="flake8"
elif python3 -c "import flake8" >/dev/null 2>&1; then
  PY_FLAKE8="python3 -m flake8"
else
  PY_FLAKE8=""
fi

if command -v isort >/dev/null 2>&1; then
  PY_ISORT="isort"
elif python3 -c "import isort" >/dev/null 2>&1; then
  PY_ISORT="python3 -m isort"
else
  PY_ISORT=""
fi

FIX_MODE=false
QUIET=true

while [ $# -gt 0 ]; do
  case "$1" in
    --fix)
      FIX_MODE=true
      shift
      ;;
    --quiet | -q)
      QUIET=true
      shift
      ;;
    --verbose | -v)
      QUIET=false
      shift
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
    *)
      break
      ;;
  esac
done

LINT_GLOBS=(
  '*.c' '*.cc' '*.cpp' '*.cxx'
  '*.h' '*.hh' '*.hpp' '*.hxx'
  '*.proto' '*.yaml' '*.yml' '*.py'
)

log() {
  if [ "$QUIET" = false ]; then
    echo "$@"
  fi
}

log_fix() {
  log "$@"
  FIXED_COUNT=$((FIXED_COUNT + 1))
}

collect_files_since_develop() {
  local target_branch="origin/develop"
  if ! git rev-parse --verify "$target_branch" >/dev/null 2>&1; then
    target_branch="develop"
  fi
  if ! git rev-parse --verify "$target_branch" >/dev/null 2>&1; then
    echo "Could not find develop or origin/develop for diff base." >&2
    return 1
  fi

  local merge_base
  merge_base=$(git merge-base HEAD "$target_branch")

  mapfile -d '' FILES < <( (
    git diff --name-only -z "$merge_base" -- "${LINT_GLOBS[@]}"
    git ls-files --others --exclude-standard -z -- "${LINT_GLOBS[@]}"
  ) | sort -zu )
}

# If no files provided, lint only changes since the branch diverged from develop.
if [ $# -eq 0 ]; then
  collect_files_since_develop
else
  FILES=("$@")
fi

if [ ${#FILES[@]} -eq 0 ]; then
  log "No relevant changed files found to check."
  exit 0
fi

log "Checking ${#FILES[@]} changed file(s) since develop..."

fail=0
FIXED_COUNT=0

for f in "${FILES[@]}"; do
  # Skip non-existent files
  [ -f "$f" ] || continue

  case "$f" in
    bazel-*/* | external/*) continue ;;
  esac

  # Only check text files
  if ! grep -Iq . "$f" 2>/dev/null; then
    continue
  fi

  # YAML validation
  if [[ "$f" =~ \.(yaml|yml)$ ]]; then
    if ! python3 -c "import yaml; yaml.safe_load_all(open('$f'))" >/dev/null 2>&1; then
      echo "YAML syntax error in $f" >&2
      fail=1
    fi
  fi

  # C/C++/Proto clang-format check/fix
  if [[ "$f" =~ \.(c|cc|cpp|cxx|h|hh|hpp|hxx|proto)$ ]]; then
    if [ "$FIX_MODE" = true ]; then
      if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
        clang-format -i "$f"
        log_fix "Fixed clang-format issues in $f"
      fi
    else
      if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
        echo "clang-format issues in $f (run hooks/lint_check.sh --fix to update)" >&2
        fail=1
      fi
    fi
  fi

  # Python lint/format
  if [[ "$f" =~ \.py$ ]]; then
    if [ "$FIX_MODE" = true ]; then
      if [ -n "$PY_ISORT" ]; then
        if ! $PY_ISORT --profile black --check-only "$f" >/dev/null 2>&1; then
          $PY_ISORT --profile black "$f" >/dev/null 2>&1 || true
          log_fix "Fixed isort issues in $f"
        fi
      fi
      if [ -n "$PY_BLACK" ]; then
        if ! $PY_BLACK --check "$f" >/dev/null 2>&1; then
          $PY_BLACK -q "$f" >/dev/null 2>&1 || true
          log_fix "Fixed black formatting in $f"
        fi
      else
        echo "black not found; skipping auto-format for $f" >&2
      fi
      if [ -n "$PY_FLAKE8" ]; then
        if ! $PY_FLAKE8 --max-line-length=88 --extend-ignore=E203 "$f" >/dev/null 2>&1; then
          echo "flake8 issues in $f" >&2
          fail=1
        fi
      else
        echo "flake8 not found; skipping lint for $f" >&2
      fi
    else
      if [ -n "$PY_BLACK" ]; then
        if ! $PY_BLACK --check "$f" >/dev/null 2>&1; then
          echo "black formatting issues in $f (run hooks/lint_check.sh --fix to update)" >&2
          fail=1
        fi
      else
        echo "black not found; skipping format check for $f" >&2
      fi
      if [ -n "$PY_FLAKE8" ]; then
        if ! $PY_FLAKE8 --max-line-length=88 --extend-ignore=E203 "$f" >/dev/null 2>&1; then
          echo "flake8 issues in $f" >&2
          fail=1
        fi
      else
        echo "flake8 not found; skipping lint for $f" >&2
      fi
    fi
  fi

  # Trailing whitespace check/fix
  if grep -nP "\s+$" "$f" >/dev/null 2>&1; then
    if [ "$FIX_MODE" = true ]; then
      sed -i 's/[[:space:]]*$//' "$f"
      log_fix "Fixed trailing whitespace in $f"
    else
      echo "trailing whitespace in $f" >&2
      fail=1
    fi
  fi

  # EOF newline check/fix (only if non-empty)
  if [ -s "$f" ]; then
    last_char=$(tail -c 1 "$f" 2>/dev/null || true)
    if [ -n "$last_char" ] && [ "$last_char" != $'\n' ]; then
      if [ "$FIX_MODE" = true ]; then
        echo "" >>"$f"
        log_fix "Fixed missing newline at EOF in $f"
      else
        echo "missing newline at EOF in $f" >&2
        fail=1
      fi
    fi
  fi
done

if [ "$fail" -eq 0 ]; then
  if [ "$FIX_MODE" = true ]; then
    if [ "$FIXED_COUNT" -gt 0 ]; then
      log "Fixed issues in $FIXED_COUNT file(s)."
    else
      log "No formatting changes needed."
    fi
  else
    log "All changed files passed linting checks."
  fi
fi

exit $fail
