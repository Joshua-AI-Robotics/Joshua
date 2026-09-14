#!/usr/bin/env bash
# Link Joshua's repo-owned skills into the skill directories your coding agents
# actually index, so they can be invoked.
#
# docs/skills/ is the single source of truth. A skill placed there is NOT
# discoverable on its own — each agent reads skills from its own directory
# (Codex $CODEX_HOME/skills, Claude Code ~/.claude/skills, ...). This script
# symlinks every docs/skills/joshua-* into those directories. Run it once per
# environment (and again after adding a skill). See docs/skills/README.md.
#
# The link points back at the repo, so editing a skill in docs/skills/ updates
# what every agent sees — the repo stays the source of truth. This script never
# writes into docs/skills/. Symlinks only: Joshua development is Ubuntu-only, so
# symlinks are always available; a copy mode would drift from the source and is
# deliberately not offered.
#
# Usage:
#   scripts/install-skills.sh            # symlink into detected agent skill dirs
#   scripts/install-skills.sh --dry-run  # print what would happen, change nothing
#   scripts/install-skills.sh --force    # replace a real (non-symlink) dest
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/docs/skills"

DRY_RUN=0
FORCE=0
for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=1 ;;
    --force)   FORCE=1 ;;
    -h|--help)
      sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *)
      echo "unknown argument: $arg" >&2
      exit 2 ;;
  esac
done

if [[ ! -d "$SRC" ]]; then
  echo "no $SRC/ — nothing to install" >&2
  exit 0
fi

# Candidate agent skill directories. A directory is used only if its parent
# (the agent's home) already exists, so we never create a skill dir for an agent
# that is not installed. Add new agents here as their skill paths are confirmed.
CODEX_HOME="${CODEX_HOME:-$HOME/.codex}"
TARGETS=(
  "$CODEX_HOME/skills"      # Codex
  "$HOME/.claude/skills"    # Claude Code
)

# Collect the skills to install: docs/skills/joshua-* that contain a SKILL.md.
mapfile -t SKILLS < <(find "$SRC" -mindepth 1 -maxdepth 1 -type d -name 'joshua-*' | sort)
if [[ ${#SKILLS[@]} -eq 0 ]]; then
  echo "no joshua-* skills in $SRC/ yet" >&2
  exit 0
fi

# Remove links this script previously created that no longer have a source —
# e.g. a skill renamed or deleted in the repo. Only touches symlinks under the
# target that point back into our source tree, never real files or foreign
# links, so a stale broken link can't crash an agent's skill loader.
prune_stale() {
  local target_dir="$1" link name src
  [[ -d "$target_dir" ]] || return 0
  while IFS= read -r link; do
    [[ -z "$link" ]] && continue
    src="$(readlink "$link")"
    case "$src" in
      "$SRC"/*) ;;                      # only our own links
      *) continue ;;
    esac
    name="$(basename "$link")"
    if [[ ! -d "$SRC/$name" ]]; then
      if [[ $DRY_RUN -eq 1 ]]; then
        echo "would prune stale: $link -> $src"
      else
        rm -f "$link"
        echo "pruned stale: $link"
      fi
    fi
  done < <(find "$target_dir" -mindepth 1 -maxdepth 1 -type l -name 'joshua-*')
}

link_one() {
  local skill_dir="$1" target_dir="$2"
  local name dest
  name="$(basename "$skill_dir")"
  dest="$target_dir/$name"

  # Refuse to clobber a real directory/file that isn't one of our symlinks,
  # unless --force. A symlink (typically our own from a prior run) is safe to
  # replace.
  if [[ -e "$dest" && ! -L "$dest" && $FORCE -eq 0 ]]; then
    echo "skip: $dest exists and is not a symlink — pass --force to overwrite" >&2
    return
  fi

  if [[ $DRY_RUN -eq 1 ]]; then
    echo "would symlink: $dest -> $skill_dir"
    return
  fi

  mkdir -p "$target_dir"
  rm -rf "$dest"
  ln -s "$skill_dir" "$dest"
  echo "symlink: $dest -> $skill_dir"
}

installed_any=0
for target in "${TARGETS[@]}"; do
  parent="$(dirname "$target")"
  if [[ ! -d "$parent" ]]; then
    echo "skip: $parent not present (agent not installed here)"
    continue
  fi
  installed_any=1
  prune_stale "$target"
  for skill in "${SKILLS[@]}"; do
    [[ -f "$skill/SKILL.md" ]] || continue
    link_one "$skill" "$target"
  done
done

if [[ $installed_any -eq 0 ]]; then
  echo "" >&2
  echo "No agent skill directories detected. Skills remain in $SRC/;" >&2
  echo "install an agent, or extend TARGETS in $0, then re-run." >&2
fi
