# Firmware

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read the local
[README.md](README.md) before editing. This file owns the rules and architecture
contract for `firmware/`; child directories inherit it unless they add local rules.

## Architecture contract

Shared codecs, session state, and portable dispatch belong in `common/`;
board adapters own platform I/O, GPIO mapping, and build artifacts. Reuse the
portable core across boards rather than forking wire formats. Host bindings
remain in `robot/board` and `robot/comm`, with a compatible checked-in protocol.
The normative [JoshuaWire contract](../docs/JOSHUA_WIRE.md) owns wire semantics;
READMEs describe board builds, wiring, and qualification limits.

Preserve exact-retry, session/reset, fault/ESTOP, and explicit recovery behavior.
Protocol callbacks need bounded work and explicit resource staging/rollback.
Changes to wire layouts require compatible schema/version handling, host tests,
and golden bytes. Keep simulated output state distinct from measured motion.

## Verification

Use native firmware/host interoperability tests with controlled clocks and fake
GPIO/serial backends, then board cross-builds when applicable. Cross-builds and
native tests do not qualify physical timing or stopping. Never flash, recover,
or enable a board as part of automated verification; follow root authorization
and the board README for deliberate hardware work.
