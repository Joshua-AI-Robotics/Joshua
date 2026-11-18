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

## Instruction files

Root instructions apply repository-wide; local instructions add only their
owned directory's contracts and are inherited by descendants. A deeper file
must not silently contradict a parent contract. Read applicable files even when
an agent starts at the repository root.

Add a local README or `AGENTS.md` when a subsystem has distinct usage or rules;
smaller folders inherit their parent guidance. Use `AGENTS.md` as the single
instruction file for local rules. Keep README
usage and examples separate, linking to the owning rules rather than repeating
them. Tool-specific settings or existing pointers may route to `AGENTS.md` but
must not define separate engineering rules. Keep the root Gemini context
configuration pointed at `AGENTS.md`. Add a README entry and architecture-map
link when introducing another directory contract. The instruction checker
validates links and routing targets, not whether an agent complied.

## Verification

Update [README.md](README.md) when a guide is added, moved, or removed. Check
local links/anchors, `hooks/agents_doc_check.sh`, and `hooks/skills_doc_check.sh`
for changed instructions/skills. Documentation-only changes need no robot run.
