---
name: joshua-add-subsystem-doc
description: Create or update a subsystem AGENTS.md with its purpose, architecture, rules, usage, navigation, and verification.
---

# Add a subsystem guide

## When to use

- A subsystem lacks an `AGENTS.md` or has incomplete local context.
- A new ownership boundary needs architecture, usage, or verification guidance.
- Existing directory documentation needs consolidation into its canonical file.

Follow the [documentation contract](../../AGENTS.md) and root
[repository instructions](../../../AGENTS.md). Smaller folders inherit their
ancestor files; add local guidance only when it has distinct information.

## Steps

1. Read applicable ancestor `AGENTS.md` files and inspect the implementation,
   interfaces, callers, and tests. Keep each fact at its owning scope.
2. Start from [the subsystem template](references/subsystem-agents-template.md)
   and write `<subsystem>/AGENTS.md`. Combine purpose, architecture, dependencies,
   rules, contents, usage/examples, safety limits, and verification. Remove
   inapplicable sections and preserve useful existing procedures.
3. Match neighboring [tools](../../../tools/AGENTS.md) and
   [AI](../../../ai/AGENTS.md) guidance. Use links relative to the destination
   file. Link shared contracts rather than copying their rules.
4. If the subsystem can open devices or move motors, state that and link the
   root [hardware-safety rules](../../../AGENTS.md#hardware-safety--read-this-first).
   Keep physical qualification limits and deliberate flashing steps explicit.
5. Link the guide from the nearest ancestor's directory navigation. Update the
   [architecture map](../../ARCHITECTURE.md#directory-contracts) for new ownership
   boundaries and the [guide index](../../AGENTS.md) when appropriate.
6. Run `hooks/agents_doc_check.sh` and `hooks/skills_doc_check.sh`; verify links
   and section anchors after moving content. Do not run hardware or flash
   firmware to verify documentation.

## References

- [Subsystem template](references/subsystem-agents-template.md)
- [Documentation contract and guide index](../../AGENTS.md)
