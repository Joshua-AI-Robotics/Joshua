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
    git ls-files -z -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' '*.proto' '*.yaml' '*.yml'; \
    git ls-files --others --exclude-standard -z -- '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' '*.proto' '*.yaml' '*.yml' \
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
