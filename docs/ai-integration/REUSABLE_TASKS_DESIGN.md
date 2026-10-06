# Reusable task design

**Status:** Proposed design for discussion
**Checkpoints:** 2. First verified task; 3. Chat-defined action groups

## Goal

Turn a user's request into a precise, inspectable task that can be saved,
repeated, and refined through conversation.

## Proposed design

Separate configured hardware, tested backend operations, and reusable task
definitions. Tasks reference the robot configuration; they do not copy wiring,
driver settings, or robot limits into another configuration format.

A task identifies its target, supported movement, parameters, cycle definition,
repetition budget, expected observations, and failure behavior. Units and
calibration must be explicit. A name such as “bench arm” resolves to a stable
configured target, with clarification when ambiguous.

For a base-joint task, a cycle could mean moving between two validated positions
and returning. The preview states the exact positions and selected speed.
The backend counts completed cycles using the required feedback.

“Run it 20 times” or “Make it slower” produces a revised, validated plan.
Changing robots requires compatible capabilities and valid bindings. Each new
real-motion run retains the required direct operator confirmation.

The LLM proposes task parameters and, later, ordered action groups. Start by
reusing existing operation schemas and readable saved parameters. A new general
task language is not required for the first proof.

Reliable repetition runs the complete validated plan as one bounded backend
operation or backend workflow using supported operations. The operator approves
that run, including its parameters, variations, and repetition budget. Changes
and restarts require fresh confirmation. Progress, cancellation, and failure
behavior reuse the authoritative backend lifecycle; the MCP adapter stays thin.

## Suggested PR scopes

1. One task's target binding, parameter validation, preview, and measured result.
2. Saving parameters and conversational refinement using existing schemas.
3. Ordered action groups, bounded repetition, parameter variations, and accurate
   completion records.

Begin with the composition needed for one task. An advanced script interface
can later call those same operations.

## Decisions for review

What identifies an arm and its joints? What is one completed cycle? Where does
task execution belong, and what changes invalidate a prepared run?

Related: [AI integration guide](../AI_INTEGRATION.md),
[hardware/MCP design](HARDWARE_MCP_DESIGN.md), and
[observability design](OBSERVABILITY_DESIGN.md).
