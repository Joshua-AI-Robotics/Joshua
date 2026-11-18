# Agent instruction routing reference

The adopted instruction format is maintained in
[docs/AGENTS.md](AGENTS.md#instruction-files). This path is retained
for existing references; it does not define a second set of rules.

## 7. Nested AGENTS.md for subsystems

Directory architecture, dependencies, lifecycle rules, and verification guidance
live together in that directory's `AGENTS.md`. See the
[architecture map](ARCHITECTURE.md#directory-contracts) for owners and the
[coding guidelines](CODING_GUIDELINES.md#where-rules-live) for shared principles.
Descriptions, usage, examples, and current behavior live in that same file.

The [instruction-link guard](../hooks/agents_doc_check.sh) discovers local instruction
files and validates their links. It also checks root routing targets and
Gemini's context setting. Source code, build visibility,
and behavioral tests remain the mechanisms for enforcing code contracts.
