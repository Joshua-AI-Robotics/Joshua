# Firmware README template

Every `firmware/<board>/README.md` (and versioned subdirectories like
`firmware/teensy/41/README.md`) should follow this section structure, so
"how do I flash board X" is always answered the same way regardless of
which board it is. Copy this file, fill in each section, delete any
instruction text (like this line). Where you genuinely don't know an
answer yet, write `TODO` rather than guessing or omitting the section —
an explicit `TODO` is honest; a missing section reads as "not needed,"
which is usually wrong.

---

# <Board name> — <firmware name>

One or two sentences: what this firmware is, what it runs on, and — if
applicable — which Joshua `BoardType` / host class it pairs with (e.g.
`TEENSY41` / `robot/board/teensy/teensy_board.*`).

## Status

Checklist of what's actually been done and verified, not what's planned.
Use `[x]` only for something you personally ran and confirmed; `⬜` for
not yet attempted. Include real evidence (a log line, a returned value)
for the checked items where practical — "verified" without evidence just
becomes a stale claim someone has to re-check anyway.

- ⬜ Toolchain installed
- ⬜ Firmware built
- ⬜ Firmware flashed
- ⬜ Board enumerates / comes up correctly
- ⬜ Protocol/handshake verified against the host
- ⬜ Full command path verified end to end

## Prerequisites

What must exist on the machine *before* you can build or flash — both
hardware and software. Be exhaustive; this is the list someone on a fresh
machine needs.

- Hardware: TODO (board revision, cables, any required accessories)
- OS packages: TODO
- Toolchain / SDK: TODO (name + exact version, since board bring-up is
  often version-sensitive)
- Accounts: TODO (e.g. a vendor account needed to download an SDK)

## Install

Exact commands to install the toolchain from a clean machine.

```bash
TODO
```

## Build

Exact commands, run from where, producing what artifact.

```bash
TODO
```

## Flash

Exact commands, plus any one-time OS-level setup (udev rules, permissions,
boot-mode switches, drivers) — and call out anything that needs to be
undone/reset afterward (e.g. switching a boot-mode DIP switch back).

```bash
TODO
```

## Verify

How to confirm the flash actually worked, in increasing order of
confidence: enumeration/liveness first, then a protocol-level handshake,
then a real command round-trip if one exists. Prefer commands the next
person can literally copy-paste and get a pass/fail signal from.

```bash
TODO
```

## Wiring / Pinout

Only if this firmware drives external hardware. Point at the actual
pinout contract (e.g. a channel table file) rather than duplicating pin
numbers here if that file is the source of truth — one place to update.

TODO

## Known gaps / Troubleshooting

Open issues, things that are approximated rather than measured, timeouts
that are guesses rather than tuned values, and anything a real hardware
run has surfaced that isn't fixed yet. Update this section every time you
actually run against hardware — it's where hard-won debugging knowledge
should live so the next person doesn't rediscover it.

- TODO

## Related files

- TODO (scripts, source paths, paired host-side class)
