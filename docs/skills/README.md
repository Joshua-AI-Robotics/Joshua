# Skills

Repo-owned **skills** — procedural playbooks that both coding agents and humans
follow to do a recurring Joshua task the same way every time. This directory is
the **single source of truth**; `scripts/install-skills.sh` links each skill
into the agent skill directories that are actually indexed, so an agent can
invoke it.

These are Joshua's own skills, named `joshua-*`. They are **not** the external
`superpowers:*` skills you see referenced in [plans](../superpowers/plans) —
those come from an installed framework. Naming ours `joshua-*` and keeping them
under `docs/skills/` (not `docs/superpowers/`) keeps the two from being
confused for one another.

## Index

| Skill | Use it when | Audience |
|-------|-------------|----------|
| [joshua-add-subsystem-doc](joshua-add-subsystem-doc/SKILL.md) | Adding or filling in a subsystem `README.md` (or nested `AGENTS.md`) | agents + humans |

## How skills work here

- **Source of truth:** each skill is a directory `joshua-<name>/` holding a
  `SKILL.md` (the executable entrypoint) and an optional `references/` folder
  for templates, examples, and checklists.
- **Invocation:** run `scripts/install-skills.sh` once in your environment to
  link every `joshua-*` skill into the skill directories your agents read.
  Supported today: Codex (`$CODEX_HOME/skills`) and Claude Code
  (`~/.claude/skills`); it installs only for agents present on the machine.
  Other agents (e.g. Gemini/Antigravity) can be added to `TARGETS` in the
  script once their skill path is confirmed. Placement under `docs/` alone does
  **not** make a skill discoverable — the install step is what does.
- **Discovery in context:** a skill is referenced from the subsystem docs where
  it applies (for example, a hardware-bring-up skill would be linked from the
  relevant `README.md`/`AGENTS.md`), so an agent touching that area meets it
  just in time — rather than every agent loading a global list on every task.

## Authoring a skill

1. Create `docs/skills/joshua-<name>/SKILL.md` with frontmatter and a body:

   ```markdown
   ---
   name: joshua-<name>
   description: <one line — when to use this skill>
   ---

   # <Title>

   ## When to use
   ## Steps
   ## References
   ```

   Keep `SKILL.md` terse and procedural — it is the artifact agents execute.
   Put human orientation and background in this README, not in each skill.

2. Put any templates or examples under `joshua-<name>/references/` and link to
   them from the skill.
3. Add a row to the [Index](#index) above.
4. Reference the skill from the subsystem doc where it applies.
5. Run `hooks/skills_doc_check.sh` — it is blocking in CI and verifies
   frontmatter, indexing, and that every link inside the skill resolves.

The `name:` in the frontmatter must match the directory name.
