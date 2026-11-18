---
name: joshua-add-subsystem-doc
description: Use when adding or filling in a subsystem README.md (or a nested AGENTS.md) so it matches Joshua's doc conventions and the directory documentation contract.
---

# Add a subsystem doc

## When to use

- A directory under the repo root has code but no `README.md`.
- An existing subsystem `README.md` is missing the sections agents rely on.
- A subsystem needs a nested `AGENTS.md` for its local architecture and rules;
  follow the [instruction-file contract](../../AGENTS.md#instruction-files).

Do **not** use this for the root `AGENTS.md` — that file is governed directly by
[repository AGENTS.md](../../../AGENTS.md), not this skill.

## Steps

1. **Decide the audience split.** `README.md` is for humans (what the subsystem
   is, how it fits, quick starts). A nested `AGENTS.md` carries agent-operational
   deltas (build/test invocations, "never do X here"). Do not duplicate rules
   across them — state each fact once. See [documentation ownership](../../AGENTS.md#architecture-contract).

2. **Start from the template.** Copy
   [references/subsystem-readme-template.md](references/subsystem-readme-template.md)
   to `<subsystem>/README.md` and fill each section. Delete sections that do not
   apply rather than leaving them empty.

3. **Match the house style.** Read a neighbouring README first
   ([tools/README.md](../../../tools/README.md) and [ai/README.md](../../../ai/README.md)
   are good models) and follow its tone, section order, and link style. Use
   repo-relative Markdown links.

4. **Flag hardware honestly.** If anything in the subsystem can open a real
   device or move a motor, say so and link the hardware-safety section of
   [AGENTS.md](../../../AGENTS.md). Never imply a path is simulation-only unless
   you have confirmed it.

5. **Wire it up.** Add the new doc to the "Read before you edit" list in
   [AGENTS.md](../../../AGENTS.md) and to the table in
   [docs/README.md](../../README.md) if it belongs there.

6. **If you added a nested `AGENTS.md`,** link it from the local README and
   architecture map. Keep local rules in that file; do not create a tool-specific
   instruction copy.

7. **Verify.** Run `hooks/agents_doc_check.sh` so instruction links and routing
   targets still resolve. Do not run hardware or flash firmware to verify docs.

## References

- Template: [references/subsystem-readme-template.md](references/subsystem-readme-template.md)
- Doc contract and nested-file rules: [docs/AGENTS.md](../../AGENTS.md)
- Doc index this may need updating: [docs/README.md](../../README.md)
