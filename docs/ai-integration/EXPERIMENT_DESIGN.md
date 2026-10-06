# Action groups and experiment workflows

**Status:** Proposed future design for discussion
**Checkpoint:** 3. Compose, repeat, and vary actions through chat

## Goal

A user defines a group of supported robot actions through MCP chat, refines it,
and repeats it with selected parameter variations. Begin with one demonstrable
workflow using the existing client and backend contracts.

## Proposed design

The LLM discovers available operations, clarifies the target and movement,
proposes the order and parameters, and presents an inspectable plan. It can
save and refine that plan through conversation.

For example: “Rotate the base, extend to the approved position, and return.
Repeat the group ten times at each of three approved speeds.” Joshua resolves
each instruction to a supported operation and checks the full plan before a
real-motion run.

For a short prototype, the client may sequence existing MCP operations under
their documented run and authorization rules. Reliable repetition uses a
bounded backend operation or reviewed workflow calling those operations, with
explicit run scope, operator confirmation, progress, cancellation, and failure
behavior. Feedback determines which actions and groups completed.

Keep the first execution path small. Use existing operation schemas and saved
parameters; add only the replay support required by the example. A dedicated
experiment service, general task language, scheduler, and statistical analysis
can follow when concrete requirements justify them.

## Suggested PR scopes

1. LLM guidance and preview for one ordered group of supported actions.
2. Saving/refinement, finite parameter variations, and bounded replay.
3. Completion records, interruption/failure tests, and the client walkthrough.

This depends on the verified operation and observations from checkpoint 2.
Required operation feedback remains available; broad inference and bulk data
collection stay outside the first MCP release.

## Decisions for review

Can the chosen client compose the example with existing tools? What minimal
replay support is missing? What defines one completed group and the approved
run boundary?

Related: [AI integration guide](../AI_INTEGRATION.md),
[reusable tasks](REUSABLE_TASKS_DESIGN.md), and
[observability](OBSERVABILITY_DESIGN.md).

Design reference: [Anthropic's guidance on composable agent workflows](https://www.anthropic.com/engineering/building-effective-agents).
The execution choice for Joshua still requires its own contract and tests.
