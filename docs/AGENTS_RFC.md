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
bridges for tools that cannot read it, and nothing else.** It records what
that file should grow into, and — equally important — what it must not.

It covers the canonical-file-plus-bridges pattern, what belongs in `AGENTS.md`
versus `CONTRIBUTING.md` versus subsystem docs, hardware-safety instructions,
multi-agent conventions, nested per-directory files (§7), and a rot guard.
Rejected directions are in §6. Nothing here requires a subsystem to adopt a
nested `AGENTS.md` today.

## 2. Where Joshua is today

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
   but never said that `joshua_main` can open real serial buses and move real
   motors. An agent asked to "verify the change" had no reason not to.
2. **Bridges are only as strong as their weakest reader.** A pointer file is
   inert text for surfaces that do not dereference links.
3. **Nothing prevents rot.** Every path in `AGENTS.md` resolves today because
   it was checked by hand once.
4. **Agent ownership is ambiguous.** `codex/docker-first-entrypoints` names a
   tool; every other branch names a person.

## 3. Design goals

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

## 4. Adopted: hardware safety and attribution

Landed in [PR #69](https://github.com/Joshua-AI-Robotics/Joshua/pull/69). The
rules themselves live in [AGENTS.md](../AGENTS.md) and are deliberately not
restated here. What follows is only the reasoning behind them, which has no
home in an instruction file.

**Why the safety rule is turn-scoped.** "Require a human present" fails against
an autonomous agent: it cannot verify presence and will assume it. Scoping
consent to the current turn is checkable — the agent either was just asked or
was not.

**Why it is not keyed to filenames.** An earlier draft treated any preset
without `sim` in its name as dangerous. The launcher branches on
`general.operation_mode`, not the name, and the categories cross:
`so100/sim_mirror.pbtxt` is `MODE_SIMULATION` yet opens `/dev/ttyACM1`, while
`example/mock_py_test.pbtxt` is `MODE_INFERENCE` yet drives only `MOCK_*`
components. The heuristic was therefore wrong in the *unsafe* direction — it
labelled a real-serial-device preset as the safe default. Agents are told to
read the preset instead.

**Why attribution is two bullets, not a section.** Process rules that hooks and
CI do not enforce are frequently ignored, and every line costs context on every
invocation. Branch naming stays operator-owned (`<github-username>/<topic>`)
because an agent-named branch obscures who is accountable and who reviews it.

## 5. Rot guard

`AGENTS.md` routes agents to other docs, so a renamed target turns it into a
confidently wrong instruction file — worse than none, because an agent will
follow it. `hooks/agents_doc_check.sh` runs in CI on every PR and fails if any
Markdown link target in the instruction files no longer resolves.

Scope is deliberately narrow: **explicit Markdown link targets only.** Paths
and commands in prose or code blocks are not checked, because docs
legitimately reference planned work — `tools/flash/` appears in
[firmware/README.md](../firmware/README.md) and
[BOARD_LAYER_RFC.md](BOARD_LAYER_RFC.md) — and placeholders like
`<github-username>/<topic>`. A guard that scraped paths would fail on those and
be disabled within a month.

Compose services and Bazel targets could later be validated with
`docker compose config --services` and `bazel query`, but only against an
explicit allowlist in the guard, never by scraping the Markdown.

## 6. Rejected alternatives

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
see §8.

## 7. Nested `AGENTS.md` for subsystems

The [agents.md](https://agents.md/) standard supports per-directory files,
loaded automatically, **closest-file-wins**. This is an endorsed pattern for
Joshua. None are required today; add one when a subsystem earns it.

### 7.1 Why it does not compete with `README.md`

The standard is explicit that the two are complementary, not alternatives:
`README.md` is for humans — quick starts, project description, contribution
guidance — while `AGENTS.md` carries *"the extra, sometimes detailed context
coding agents need: build steps, tests, and conventions that might clutter a
README or aren't relevant to human contributors."*

So this is a **separation by audience, not duplication**. `ai/README.md`
illustrates the seam already: "Inference architecture" is narrative a human
wants, while "Adding a model", "Checklist", and "Bazel vs runtime deps" are
procedures and rules an agent executes. Roughly half that file is nested-
`AGENTS.md` material sitting in a human doc.

Renaming READMEs to `AGENTS.md` is **not** the answer: GitHub renders
`README.md` on the directory page and does not render `AGENTS.md`, so humans
would lose the front door, and agents would swallow narrative prose they do not
need.

### 7.2 When a subsystem earns one

Add `<dir>/AGENTS.md` when the directory has agent-operational content that
either clutters the README or has no place in it:

- Build or test invocations specific to that subsystem.
- Conventions with a failure mode — "never do X here", ordering requirements,
  generated files that must not be hand-edited.
- Hardware, safety, or bring-up constraints local to that directory.

Do **not** add one merely to have one. A directory with no docs at all needs a
`README.md` first — that serves both audiences and is the larger gap.

### 7.3 How to write one

Files **concatenate from the root down**; they do not replace each other. Both
ecosystems agree on this — the agents.md hierarchy merges with later files
winning on conflicts, and Claude Code states that discovered files "are
concatenated into context rather than overriding each other." So a root rule
still applies inside a subsystem. The hazard is narrower than
whole-file precedence: only a nested rule that *contradicts* a root rule
silently wins. Two rules keep that safe:

1. **Local deltas only.** State what is true *here* and not elsewhere. Never
   copy a root rule — especially not the hardware-safety rules, which must have
   exactly one source.
2. **Defer upward explicitly.** Open with a line pointing at the root file, so
   an agent that loads only the nested file still knows the global rules apply.

```markdown
# AGENTS.md — <subsystem>

Also follow the root [AGENTS.md](../AGENTS.md); this file adds only what is
specific to `<subsystem>/`.

- <local rule>
- <local build/test invocation>
```

Adjust the `../` to the directory's depth — `robot/comm/ethercat/AGENTS.md`
needs `../../../AGENTS.md`. `hooks/agents_doc_check.sh` fails on a wrong one.

Keep them short. A nested file that grows past a screen is usually a README
trying to escape.

### 7.4 Safety rules never move into a nested file

§7.3 rule 1 forbids copying the hardware-safety rules downward. There is a
mechanical reason beyond single-sourcing: **a root instruction file survives
context compaction and a nested one does not.** Claude Code re-injects the
project-root file after `/compact`, while nested files "are not re-injected
automatically; they reload the next time Claude reads a file in that
subdirectory."

A long session that compacts mid-task therefore keeps the root rules and drops
the nested ones until the next read in that directory. For conventions that is
a small loss. For "do not run a preset that opens a real serial bus" it is the
window in which the accident happens. Hardware safety stays in the root
`AGENTS.md`, permanently, whatever else nests.

### 7.5 Every nested `AGENTS.md` needs a `CLAUDE.md` beside it

Claude Code does not read `AGENTS.md` at any level, and it is the agent that
writes most of this repository. The root bridge does not cover subdirectories,
so a bare `robot/AGENTS.md` would be invisible to it while Codex, Cursor, and
Antigravity read it — a silent two-tier split, worst in exactly the subsystems
that earn a nested file.

So a nested `AGENTS.md` is always a pair:

```bash
printf '@AGENTS.md\n' > robot/CLAUDE.md
```

`@` imports resolve relative to the importing file, so `robot/CLAUDE.md` picks
up `robot/AGENTS.md`. `hooks/agents_doc_check.sh` fails when the bridge is
missing, so the pair cannot drift apart.

No nested `GEMINI.md` or Copilot bridge. Gemini reads nested files already, and
Copilot's path-scoped instructions (`.github/instructions/*.instructions.md`
with an `applyTo:` glob) would cover the whole tree from one central directory
if it ever becomes worth doing — neither needs a file per subsystem.

This is the pattern Datadog's frontend team landed on for the same problem,
after finding that nested files alone "only work when users edit specific
sub-folders."

## 8. Open questions

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
3. **Will paired `README.md` + `AGENTS.md` actually stay in sync?** §7 keeps
   nested files to local deltas precisely because paired docs drift in practice.
   The first subsystem to adopt one is the test; if it drifts within a release,
   the guidance in §7.3 is not strong enough.
## 9. Status

Landed in [PR #69](https://github.com/Joshua-AI-Robotics/Joshua/pull/69):

- [x] Canonical `AGENTS.md` and the three pointer bridges (§2).
- [x] Hardware-safety rules and attribution conventions (§4).
- [x] `README.md` for `robot/`, `config/`, and `tools/` — the three directories
      that had docs for neither audience — and all of them added to the root
      "Read before you edit" list.
- [x] `hooks/agents_doc_check.sh`, run per-PR by the `agent-docs` CI job (§5).

Demand-driven, no schedule:

- [ ] `<dir>/AGENTS.md` where a subsystem earns one (§7.2), paired with the
      `CLAUDE.md` bridge the guard requires (§7.5).
- [ ] Resolve the Copilot bridge question (§8.1) with evidence.
- [ ] **Enforce** the hardware rule rather than stating it. §3 prefers
      enforcement to exhortation, and §4 is currently exhortation: nothing
      blocks a hardware-touching command, and no agent's compliance has been
      measured. A pre-execution guard — the shape of `scripts/push-gate.sh` —
      would close the gap this RFC otherwise only documents.

## 10. Acceptance criteria

- A fresh agent, launched from the repo root with no prior context, will not run
  a hardware-touching command without the user asking in that turn.
- Every rule is traceable to exactly one file; supporting a new agent tool takes
  one pointer file and zero rule changes.
- A renamed doc referenced by an instruction file fails CI.
