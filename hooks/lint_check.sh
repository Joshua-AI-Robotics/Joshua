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

# Check if we're in fix mode (when called with --fix)
FIX_MODE=false
if [ "${1:-}" = "--fix" ]; then
  FIX_MODE=true
  shift  # Remove --fix from arguments
fi

# If no files provided, get all relevant files
if [ $# -eq 0 ]; then
  # Build candidate file list: tracked + untracked (excluding ignored)
  mapfile -d '' FILES < <( (
    git ls-files -z -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' '*.proto' '*.yaml' '*.yml' '*.py'; \
    git ls-files --others --exclude-standard -z -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' '*.proto' '*.yaml' '*.yml' '*.py' \
  ) | sort -zu )
else
  FILES=("$@")
fi

if [ ${#FILES[@]} -eq 0 ]; then
  echo "No relevant files found to check."
  exit 0
fi

if [ "$FIX_MODE" = true ]; then
  echo "Checking ${#FILES[@]} files for issues to fix..."
else
  echo "Checking ${#FILES[@]} files for linting issues..."
fi

fail=0

for f in "${FILES[@]}"; do
  # Skip non-existent files
  [ -f "$f" ] || continue
  
  # Skip directories and non-existent
  case "$f" in
    bazel-*/*|external/*) continue ;;
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
      # Fix formatting
      if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
        clang-format -i "$f"
        echo "Fixed clang-format issues in $f"
      fi
    else
      # Check formatting
      if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
        echo "clang-format issues in $f (run hooks/lint_check.sh --fix to update)" >&2
        fail=1
      fi
    fi
  fi
  
  # Python lint/format
  if [[ "$f" =~ \.py$ ]]; then
    if [ "$FIX_MODE" = true ]; then
      # Apply import sorting first
      if [ -n "$PY_ISORT" ]; then
        $PY_ISORT --profile black "$f" >/dev/null 2>&1 || true
        echo "Sorted imports with isort in $f"
      fi
      # Format with black
      if [ -n "$PY_BLACK" ]; then
        $PY_BLACK -q "$f" >/dev/null 2>&1 || true
        echo "Formatted with black in $f"
      else
        echo "black not found; skipping auto-format for $f" >&2
      fi
      # Lint with flake8
      if [ -n "$PY_FLAKE8" ]; then
        # Black compatibility: max-line-length 88, ignore E203 (whitespace before :)
        if ! $PY_FLAKE8 --max-line-length=88 --extend-ignore=E203 "$f" >/dev/null 2>&1; then
          echo "flake8 issues in $f" >&2
          fail=1
        fi
      else
        echo "flake8 not found; skipping lint for $f" >&2
      fi
    else
      # Check formatting with black
      if [ -n "$PY_BLACK" ]; then
        if ! $PY_BLACK --check --diff "$f" >/dev/null 2>&1; then
          echo "black formatting issues in $f (run hooks/lint_check.sh --fix to update)" >&2
          fail=1
        fi
      else
        echo "black not found; skipping format check for $f" >&2
      fi
      # Lint with flake8
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
      # Fix trailing whitespace
      sed -i 's/[[:space:]]*$//' "$f"
      echo "Fixed trailing whitespace in $f"
    else
      # Check trailing whitespace
      echo "trailing whitespace in $f" >&2
      fail=1
    fi
  fi
  
  # EOF newline check/fix (only if non-empty)
  if [ -s "$f" ]; then
    last_char=$(tail -c 1 "$f" 2>/dev/null || true)
    # If tail returns nothing (shouldn't for non-empty), skip
    if [ -n "$last_char" ]; then
      if [ "$last_char" != $'\n' ]; then
        if [ "$FIX_MODE" = true ]; then
          # Fix EOF newline
          echo "" >> "$f"
          echo "Fixed missing newline at EOF in $f"
        else
          # Check EOF newline
          echo "missing newline at EOF in $f" >&2
          fail=1
        fi
      fi
    fi
  fi
done

if [ "$fail" -eq 0 ]; then
  if [ "$FIX_MODE" = true ]; then
    echo "All files are properly formatted."
  else
    echo "All files passed linting checks."
  fi
fi

exit $fail
