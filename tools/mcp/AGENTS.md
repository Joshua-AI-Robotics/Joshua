# MCP adapters

Follow the repository [AGENTS.md](../../AGENTS.md) and
[coding guidelines](../../docs/CODING_GUIDELINES.md). Read the local
[README.md](README.md) before editing. This file owns the rules and architecture
contract for `tools/mcp/`; child directories inherit it unless they add local rules.

## Architecture contract

This directory owns bounded stdio adapters and read-only observation readers.
C++ parsing/`ValidateConfig` owns config acceptance; factory tables own software
discovery; curated model support and historical test records remain distinct.
NodeManager owns runtime processes and snapshot publication. MCP consumes those
contracts through thin adapters and never initializes drivers, commands devices,
launches/stops a robot, or turns missing/stale observations into permission to move.

Keep tool-selected paths confined to the configured root. Tool arguments cannot
choose helper executables or metadata sources. Preserve bounded input/output,
helper deadlines, cancellation/reaping, and one-helper admission behavior.
Runtime snapshots preserve source identity, receipt freshness, and provenance
limitations. Do not equate process readiness, protocol acknowledgment, historical
hardware evidence, or publication timestamps with current physical readiness.

## Verification

Use the existing SDK stdio, helper-process, path-confinement, observation, and
catalog tests in [BUILD](BUILD). Exercise malformed/oversized data, cancellation,
missing/stale sources, and independent model/test-history failures. Run the
changed `//tools/mcp:..._test` targets in both Docker test stacks. Follow the
README for setup smoke checks; do not launch an operator's robot to test an adapter.
