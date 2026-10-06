---
name: joshua-add-subsystem-doc
description: Use when adding or filling in a subsystem README.md (or a nested AGENTS.md) so it matches Joshua's doc conventions and the AGENTS_RFC contract.
---

# Add a subsystem doc

## When to use

- A directory under the repo root has code but no `README.md`.
- An existing subsystem `README.md` is missing the sections agents rely on.
- A subsystem has earned a nested `AGENTS.md` (see [AGENTS_RFC](../../AGENTS_RFC.md) §7)
  and you are creating it plus its `CLAUDE.md` bridge.

Do **not** use this for the root `AGENTS.md` — that file is governed directly by
[AGENTS_RFC](../../AGENTS_RFC.md), not this skill.

## Steps

1. **Decide the audience split.** `README.md` is for humans (what the subsystem
   is, how it fits, quick starts). A nested `AGENTS.md` carries agent-operational
   deltas (build/test invocations, "never do X here"). Do not duplicate rules
   across them — state each fact once. See [AGENTS_RFC](../../AGENTS_RFC.md) §7.1.

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

6. **If you added a nested `AGENTS.md`,** create the sibling `CLAUDE.md` bridge
   (`printf '@AGENTS.md\n' > <dir>/CLAUDE.md`) — [AGENTS_RFC](../../AGENTS_RFC.md)
   §7.5 and the guard require it.

7. **Verify.** Run `hooks/agents_doc_check.sh` so every link and bridge you
   touched still resolves. Do not run hardware or flash firmware to "verify" a
   doc change.

## References

- Template: [references/subsystem-readme-template.md](references/subsystem-readme-template.md)
- Doc contract and nested-file rules: [AGENTS_RFC](../../AGENTS_RFC.md)
- Doc index this may need updating: [docs/README.md](../../README.md)
