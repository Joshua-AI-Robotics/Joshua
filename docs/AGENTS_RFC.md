# Agent Instructions RFC: One Contract for Many Coding Agents

Status: Draft (design only — no implementation in this document)
Depends on: [PR #69](https://github.com/Joshua-AI-Robotics/Joshua/pull/69),
which adds the root `AGENTS.md` this document builds on
Companion to: [CONTRIBUTING.md](../CONTRIBUTING.md) (human contributors),
[ARCHITECTURE.md](ARCHITECTURE.md)

## 1. Purpose

Several AI coding agents already work in this repository — Claude Code, Codex,
Antigravity, and Copilot all appear in branch names, commit trailers, and
review history. Each tool has its own convention for repo instructions
(`CLAUDE.md`, `GEMINI.md`, `.cursorrules`, `.github/copilot-instructions.md`).
Left alone, that produces N copies of the same rules, drifting apart, with no
one file anyone trusts.

This RFC defines the contract: **one canonical `AGENTS.md`, thin pointer
bridges for tools that cannot read it, and nothing else.** It then proposes
what that file should grow into, and — equally important — what it must not.

## 2. Scope

In scope:

- The canonical-file-plus-bridges pattern and the rule that bridges never
  contain rules.
- What belongs in `AGENTS.md` versus `CONTRIBUTING.md` versus subsystem docs.
- Hardware-safety instructions, since this repo moves physical robots.
- Multi-agent working conventions (attribution, branch ownership).
- A narrowly scoped CI guard against documentation rot.

Out of scope (deliberately, see §7):

- Per-tool rule files beyond the three bridges that exist today.
- Encoding process policy (review requirements, PR stacking mechanics) into
  prompt files rather than CI and templates.
- Nested per-directory `AGENTS.md` files — evaluated and deferred in §8.

## 3. Where Joshua is today

After [PR #69](https://github.com/Joshua-AI-Robotics/Joshua/pull/69):

```
AGENTS.md                          canonical — every rule lives here
├── CLAUDE.md                      "@AGENTS.md"        (Claude Code)
├── GEMINI.md                      pointer + import    (Gemini CLI)
└── .github/copilot-instructions.md  pointer           (Copilot IDE chat)
```

| Agent | Reads `AGENTS.md` natively | Covered by |
| --- | --- | --- |
| Codex, Cursor, Windsurf, Aider, Zed, Jules, Devin, Junie, Amp, goose, Warp | ✅ | native |
| Copilot coding agent / CLI / VS Code | ✅ | native |
| Antigravity (v1.20.3+) | ✅ | native |
| Claude Code | ❌ (Anthropic: not planned) | `CLAUDE.md` |
| Gemini CLI | ⚠️ only if `contextFileName` is set | `GEMINI.md` |
| Copilot IDE-chat surfaces | ❌ | `.github/copilot-instructions.md` |

Observed practice that this RFC codifies rather than invents:

- Branch prefixes are a mix of operators (`hmoon/` ×41, `djkim/`, `ulee/`,
  `kyoon/`, `theo/`) and one agent (`codex/` ×3).
- Agent-assisted commits already carry a `Co-Authored-By: Claude` trailer.
- Cross-model review (Codex + Antigravity) is used before pushes, but as
  personal tooling, not repo policy.

Structural problems this RFC addresses:

1. **Hardware instructions were absent.** `AGENTS.md` described test and lint
   but never said that `joshua_main` against a non-`sim` preset moves real
   motors. An agent asked to "verify the change" had no reason not to.
2. **Bridges are only as strong as their weakest reader.** A pointer file is
   inert text for surfaces that do not dereference links.
3. **Nothing prevents rot.** Every path in `AGENTS.md` resolves today because
   it was checked by hand once.
4. **Agent ownership is ambiguous.** `codex/docker-first-entrypoints` names a
   tool; every other branch names a person.

## 4. Design goals

- **One canonical file.** Rules live in `AGENTS.md`. Bridges route to it and
  contain no workflow or safety rules of their own — routing text only.
- **Route, don't restate.** `AGENTS.md` points at `CONTRIBUTING.md`, subsystem
  READMEs, and `docs/`. The moment it duplicates them, they drift and agents
  receive contradictory instructions.
- **Safety is not optional context.** Instructions that prevent physical harm
  belong in the file every agent loads, not in a subsystem doc it may not open.
- **Prefer enforcement to exhortation.** A rule an agent may ignore is worth
  less than a CI check. Process policy belongs in CI and PR templates, not in
  prompt files that consume context on every invocation.
- **Thin beats complete.** Every line is loaded into every agent's context on
  every task. Length dilutes the rules that matter.

## 5. Adopted: hardware safety and attribution

Landed in [PR #69](https://github.com/Joshua-AI-Robotics/Joshua/pull/69).

### 5.1 Hardware safety

`AGENTS.md` now opens with a hardware-safety section, before any workflow
instructions:

- Filenames are explicitly *not* the safety signal. The launcher branches on
  `general.operation_mode`, and the two categories cross: `so100/sim_mirror.pbtxt`
  is `MODE_SIMULATION` but opens `/dev/ttyACM1`, while
  `example/mock_py_test.pbtxt` is `MODE_INFERENCE` and drives only `MOCK_*`
  components. Agents are told to read the preset — `operation_mode`, device
  paths, component types — before running it.
- Running a preset that declares real hardware, and flashing firmware, require
  the user to ask **in the current turn** and confirm the hardware is set up. A
  standing "verify your work" instruction is explicitly not that confirmation.
- Tests and config validation are the default verification path; they touch no
  hardware.
- Firmware is never flashed automatically, consistent with
  [firmware/README.md](../firmware/README.md).

Two phrasings here are deliberate. "Require a human present" fails against an
autonomous agent, which cannot verify presence and will assume it. And an
earlier draft keyed the rule to `sim` in the filename — a heuristic that is
wrong in the *unsafe* direction, since a simulation preset can still open a
real serial device.

### 5.2 Agent attribution and branch ownership

- Branches are named `<github-username>/<topic>` — for the **operator**, not
  the agent. An agent-named branch obscures who is accountable for it and who
  reviews it.
- Agent-assisted commits carry a `Co-Authored-By:` trailer naming the model.

Two bullets, not a section. Process rules that are not enforced by hooks or CI
are frequently ignored, and every line costs context on every invocation.

## 6. Proposed: rot guard

`AGENTS.md` is accurate today only because it was verified by hand. Nothing
catches a renamed doc tomorrow, and a confidently wrong instruction file is
worse than none — an agent will follow it.

Proposal: a CI job that verifies **only explicit Markdown link targets** in
`AGENTS.md` and its bridges resolve.

```bash
# for each Markdown link target in AGENTS.md, GEMINI.md, CLAUDE.md:
#   assert the referenced file exists
```

Deliberately **not** checked: paths and commands mentioned in prose or code
blocks. Documentation legitimately references things that do not exist yet —
`tools/flash/` appears in [firmware/README.md](../firmware/README.md) and
[BOARD_LAYER_RFC.md](BOARD_LAYER_RFC.md) as planned work. A guard that greps
for paths would fail on placeholders (`<github-username>/<topic>`), future
work, and illustrative examples, and would be disabled within a month.

Compose services and Bazel targets could later be validated with
`docker compose config --services` and `bazel query`, but only against an
explicit allowlist in the guard, never by scraping the Markdown.

## 7. Rejected alternatives

**Per-tool rule files** (`.cursorrules`, `.windsurfrules`). Cursor and Windsurf
read `AGENTS.md` natively. Each additional file is a new copy to drift.

**Rules inside bridge files.** `GEMINI.md` is a pointer with an explicit
comment forbidding rules, because Gemini-family tools may give `GEMINI.md`
precedence over `AGENTS.md` — a rule written there would silently override the
canonical file for a subset of agents. Same principle for every bridge.

**Process policy in `AGENTS.md`** (review requirements, PR stacking mechanics,
cross-model review). Enforce in CI status checks and
[pull_request_template.md](../.github/pull_request_template.md). Agents ignore
unenforced process text, and it costs context on every task.

**Symlinking bridges to `AGENTS.md`.** Would expose full rule text to surfaces
that do not dereference links (notably Copilot inline completion). Left open —
see §9.

## 8. Deferred: nested `AGENTS.md`

The [agents.md](https://agents.md/) standard supports per-directory files,
loaded automatically, closest-file-wins. That is a stronger mechanism than the
current "Read before you edit" prose, which an agent may skip and which
silently no-ops where a directory has no README.

Deferred anyway, because the cost lands where the benefit is smallest:

- Subsystems that most need guidance (`ai/`, `ros2/`, `firmware/`,
  `simulation/`) **already have substantial READMEs** — `ai/README.md` is 193
  lines. A nested `AGENTS.md` beside it creates two documentation trees for one
  subsystem, and the next refactor updates only one.
- Subsystems with **no** README (`robot/`, `config/`, `tools/`) need
  documentation for humans too. A README serves both audiences; an
  `AGENTS.md` serves only agents and leaves humans with nothing.
- Local files that restate global rules go stale invisibly: a
  `firmware/AGENTS.md` copy of the flashing rule would survive a change to the
  root rule, and agents editing only `firmware/` would follow the stale copy.

**Instead:** write `robot/README.md`, `config/README.md`, and `tools/README.md`,
and add them to the root routing rule as part of the same change (§10, Phase
2). Revisit nested
`AGENTS.md` only for guidance that is genuinely agent-specific and has no place
in a human-facing README, and if adopted, restrict nested files to local
deltas plus an explicit "also follow the root `AGENTS.md`".

## 9. Open questions

1. **Copilot bridge fidelity.** Cross-model review split on whether
   `.github/copilot-instructions.md` should become a git symlink to
   `../AGENTS.md`. Antigravity argues Copilot ingests it as raw text and never
   follows the link, so inline completion receives one sentence and no rules;
   Codex argues Copilot's modern surfaces read `AGENTS.md` natively, making the
   pointer a legacy fallback where fidelity barely matters. The repo is
   Ubuntu-only, so symlinks are portable here. Unresolved.
2. **Should cross-model review become repo policy?** It has caught real defects
   (this document was revised substantially by it), but mandating it in CI is a
   heavier commitment than one contributor's local habit.
3. **When does a subsystem earn agent-specific instructions?** §8 defers nested
   files; the trigger for revisiting should be a concrete case where the
   guidance cannot live in a README.
4. **Does the rot guard belong on every PR or on a schedule?** Per-PR catches
   breakage immediately but adds a required check to unrelated work.

## 10. Phased rollout

### Phase 1 — Safety and attribution (landed, PR #69)
- [x] Hardware-safety section in `AGENTS.md`.
- [x] Operator-owned branch naming and `Co-Authored-By` conventions.
- [x] `GEMINI.md` bridge closing the Gemini CLI gap.

### Phase 2 — Subsystem documentation
- [ ] `robot/README.md`, `config/README.md`, `tools/README.md`, matching the
      house style of `robot/comm/ethercat/README.md` (Responsibilities /
      Non-Goals).
- [ ] Confirm the root "Read before you edit" list covers every directory that
      now has a README.

### Phase 3 — Rot guard
- [ ] CI job validating explicit Markdown link targets in `AGENTS.md` and
      bridges.
- [ ] Decide per-PR versus scheduled (§9.4).

### Phase 4 — Revisit
- [ ] Resolve the Copilot symlink question (§9.1) with evidence, not opinion.
- [ ] Re-evaluate nested `AGENTS.md` if a concrete agent-specific need appears.

## 11. Acceptance criteria

- Every rule an agent follows is traceable to exactly one file; bridges contain
  no rules.
- A fresh agent, launched from the repo root with no prior context, will not
  run a hardware-moving command without the user asking in that turn.
- A renamed or deleted doc referenced by `AGENTS.md` fails CI rather than
  silently misleading agents.
- Adding support for a new agent tool requires at most one new pointer file and
  zero changes to the rules themselves.
