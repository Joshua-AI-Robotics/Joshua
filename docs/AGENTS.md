# Documentation

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read the local
[README.md](README.md) before editing. This file owns the rules and architecture
contract for `docs/`; child directories inherit it unless they add local rules.

## Architecture contract

`CODING_GUIDELINES.md` owns shared principles; directory `AGENTS.md` files own
local rules and architecture contracts. `ARCHITECTURE.md` maps those owners and
explains system data flow. READMEs provide usage and current behavior. Keep one
normative definition of each rule and link to it from other documents.

Preserve normative wire contracts, hardware evidence, and operator procedures.
Distinguish implemented behavior from proposed capabilities and physical claims
from software-only tests. Do not add RFC/spec/implementation-plan archives under
`superpowers/`; update the owning contract and focused PR description instead.
Skills remain in `skills/` and route to the same contracts.

## Instruction files and bridges

Root instructions apply repository-wide; local instructions add only their
owned directory's contracts and are inherited by descendants. A deeper file
must not silently contradict a parent contract. Read applicable files even when
an agent starts at the repository root.

Each local `AGENTS.md` needs a sibling `CLAUDE.md` containing only `@AGENTS.md`.
Other tool bridges likewise contain routing, never their own engineering rules.
Keep the root Gemini context configuration pointed at `AGENTS.md`. Add a README
entry and an architecture-map link when introducing another directory contract.
The link/bridge checker validates discoverability, not whether an agent complied.

## Verification

Update [README.md](README.md) when a guide is added, moved, or removed. Check
local links/anchors, `hooks/agents_doc_check.sh`, and `hooks/skills_doc_check.sh`
for changed instructions/skills. Documentation-only changes need no robot run.
