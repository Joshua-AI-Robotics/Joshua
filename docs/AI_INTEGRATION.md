# AI integration

Joshua already connects protobuf configuration, ROS 2 runtime components,
model adapters, data collection, simulation, and contributor workflows. The
current implementations and example presets are indexed in the
[supported-component catalog](SUPPORTED_COMPONENTS.md). The
proposed first motion-capable Model Context Protocol (MCP) release is an optional
front end for exactly one tested, bounded robot operation. AI inference, including the
existing [inference host](../ai/README.md), and
[data collection](../ai/train/README.md) are outside its scope. This guide
records shared design rules and independent proposed follow-ups.

Any contributor may propose these changes. Reviews should include people
familiar with the affected implementation; this does not create exclusive
subsystem or runtime roles.

## Vision and interface

Joshua should help people use robotics to accelerate work they already
understand, without requiring robotics expertise. The audience includes
laboratory specialists, manufacturing operators, researchers, and prototype
builders.

Start with an MCP layer connected to an existing AI IDE or compatible chat
application. Joshua's packaged runtime and backend handle software preparation,
configuration, device access, and execution. A dedicated chat interface can
follow if needed.

**User → Chat → MCP → Joshua backend → Robot**

Connecting supported hardware should be guided. Adding new hardware requires
implementation, testing, documentation, and release through the relevant
subsystem.

## Checkpoints and requirements

| Checkpoint | Requirements | Evidence of completion |
| --- | --- | --- |
| **1. Connect and inspect through MCP** | A working client connection, installed-version/device information, configuration validation, capability descriptions, and clear errors. | An AI IDE connects to Joshua and inspects the selected configuration without starting hardware. |
| **2. Execute one verified robot task** | One bounded operation, direct operator confirmation, measured feedback with known units/freshness, progress, cancellation, and fault handling. | A task runs through MCP on one robot; measured outcomes and timeout, cancellation, and failure cases are verified. |
| **3. Compose, repeat, and vary actions through chat** | The LLM resolves an ordered group of supported actions, parameters, variations, and repetition count. A validated execution path performs bounded repetition and records outcomes. | A user defines and refines one group through chat, then runs the requested repetitions and variations with accurate completion records. |
| **4. Reach 100% hardware coverage** | Extend tested MCP integration and applicable feedback across every implemented board, transport, actuator, and sensor path. | Every implemented hardware path demonstrated through MCP. Missing or unverified integrations keep this checkpoint open. |

Ship checkpoints 1–3 incrementally using one supported environment and robot.
Inspection errors are visible from checkpoint 1; operation feedback is required
in checkpoint 2. The LLM handles action composition and refinement in checkpoint
3, while Joshua enforces operation limits and reliable execution. Start with a
small sequence and bounded repetition; a dedicated experiment framework can
follow if needed.

Hardware coverage can expand in parallel. Checkpoint 4 requires 100% coverage
of implemented paths in the runtime version under review. New hardware
additions include their corresponding MCP integration and verification.
A complete fresh-host installation journey remains a separate release gate.

## Example: Repeat a defined base-joint movement

An illustrative future task is to exercise **Bench Arm A's base joint** for
**100 return cycles**, using angles supported by that arm.

Joshua resolves a vague request into a preview: **move from 0° to 45° and back
to 0°; that is one cycle. Repeat 100 times at the selected supported speed.**
Measured feedback must confirm each endpoint within the agreed tolerance;
missing feedback or a fault ends the task through its defined stop behavior.

The user can save this as “Base movement test,” then say “Run it 20 times” or
“Make it slower.” The backend validates the revised task and handles repetition.
Saved tasks reference configured hardware and tested operations; an optional
script interface can follow later.

For checkpoint 3, a user could say, “Group the supported rotation, extension,
and return movements. Repeat the group ten times at each of three approved
speeds.” The LLM clarifies those movements and previews the resolved sequence.
Repetition executes through a bounded backend operation or reviewed workflow
calling supported operations, with an explicitly defined run/authorization
scope. Record each completed group and variation.

## Linked implementation designs

| Area | High-level design | First decisions |
| --- | --- | --- |
| Checkpoints 1, 2, and 4 | [Hardware and MCP](ai-integration/HARDWARE_MCP_DESIGN.md) | Inspection, first operation, runtime ownership, and full hardware coverage |
| Checkpoints 2–3 | [Reusable tasks](ai-integration/REUSABLE_TASKS_DESIGN.md) | Identity, movement, cycle definition, refinement, and bounded execution |
| Checkpoints 1–2 | [Observability](ai-integration/OBSERVABILITY_DESIGN.md) | Feedback source, units, freshness, completion evidence, and faults |
| Checkpoint 3 | [Action groups and experiments](ai-integration/EXPERIMENT_DESIGN.md) | LLM composition, parameter variations, bounded replay, and result records |

These briefs connect the direction to proposed PR scopes. Agree on the first
robot, task, and feedback contract, then refine the relevant design before
implementation. Start with inspection and validation, then the first verified
operation, followed by reusable action groups. Expand hardware coverage
alongside those releases.

## Relevant current capabilities

| Area | Current implementation | Limitation |
|---|---|---|
| Configuration | Protobuf schemas, presets, and [`config::ValidateConfig`](../config/README.md) | The `.pbtxt` config is the source of truth. The [web UI implementation](../ui/src/pages/ConfigPage.tsx) uses the generated schema to edit configs and parses and formats `.pbtxt`, but it does not run semantic validation. |
| Execution | The launcher, [node generator](../node_generator/README.md), ROS 2 nodes, and [simulation](../simulation/README.md) | The launcher selects the runtime path and NodeGenerator manages ROS 2 node processes. These interfaces are subsystem-specific; no general MCP-facing runtime contract is merged into `develop`. |
| Verification and safety | Targeted tests, Docker CI tasks, simulation, subsystem checks, and [hardware rules](../AGENTS.md) | Software results do not establish hardware validation. |
| Contributor workflows | Repository documentation, `AGENTS.md`, and [repository skills](skills/README.md) | Skills document and sequence existing workflows; they do not define parallel build, validation, or launch paths. |

## Design rules

1. **Use the existing protobuf schema for robot configuration.** Skills, the
   UI, and MCP tools that read or write robot configuration must use Joshua's
   existing protobuf schema and `.pbtxt` configs. Read-only inventories must
   derive configuration facts from that schema and merged presets, and other
   support facts from relevant merged sources; they do not write configuration.
   Do not create a parallel robot configuration format.
2. **Define each runtime operation in the subsystem that implements it.**
   Before a shared tool or API exposes an operation, the implementing subsystem
   must define and test its inputs, outputs, behavior, and failure cases.
   Cross-cutting tools wrap established operations rather than inventing their
   contracts; see the fuller [MCP constraints](#mcp-constraints) for MCP tools.
3. **Describe a capability as supported only after its implementation is
   merged.** The claim must also match the scope and verification level shown
   by evidence. Open pull requests can inform plans, but documentation and
   tools must not present their work as currently available.
4. **Make setup and operation accessible through prompts.** Users should be
   able to set up and operate supported robots through prompts. The frontend
   gathers intent and shows progress; Joshua's binary and backend handle
   software installation, dependencies, configuration, validation, and
   execution. Physical setup and required operator confirmation remain guided
   human steps.

## Proposed follow-ups

| Change | Start from | Intended result |
|---|---|---|
| Change-validation skill | Existing subsystem documentation and test commands; use the catalog when available | A workflow that selects relevant checks and states what each result proves, without implementing another validator. |
| Configuration skill | Existing presets, schemas, and `config::ValidateConfig` | A workflow that starts from the nearest merged preset, modifies it through existing config paths, and validates the result without launching it. |
| Layer-specific guidance | A merged and documented extension contract | Separate guidance for communication, board/GPIO, and perception because their implementations and evidence differ. |
| Guided-integration skill | Existing merged components, presets, and validation paths; use the proposed skills when available | A workflow that composes supported components into a preset. New drivers and runtime extensions remain separate changes. |
| MCP front end and operator guide | A candidate bounded robot operation to define and test; [PR #90](https://github.com/Joshua-AI-Robotics/Joshua/pull/90) is an experimental reference | Stabilize and merge one tested subsystem operation, then add an optional adapter and guide for it. |

These items describe independent proposed work, not current support or required
project phases. MCP requires only the tested contract and safeguards relevant
to each operation it exposes; it does not depend on the contributor tooling
items in this table.

PR #90 is an unmerged prototype for one actuator. It informs
this direction but does not establish current MCP support or select the first
operation.

### Proposed MCP operator guide

For that first operation, the guide should assume little hardware experience
and explain in plain language:

- how to host the adapter, authorize access, and connect an MCP client;
- how to inspect the selected preset's operation mode and every declared
  hardware endpoint (including serial paths, network interfaces, and camera
  indices), including in a preset named for simulation, then run configuration
  integrity checks before launch through a path that opens no devices. If none
  exists for that preset, the MCP follow-up must provide and test one outside
  the adapter. A normal launch is not a validation-only check, and passing
  configuration checks does not establish that hardware is ready to move;
- how to identify the board and revision as precisely as the preset and merged
  documentation permit, map its declared pins and channels to physical
  terminals and signal grounds, and identify required firmware installation or
  checks and other manual setup.
  Link the matching board-specific wiring and firmware instructions (for
  example, the [Teensy 4.1 guide](../firmware/teensy/41/README.md)) and state
  the selected board's verification status from the
  [firmware index](../firmware/README.md). The MCP adapter must not flash
  firmware;
- before real motion, how the operator confirms facts Joshua has not
  established: a physical disconnect for actuator or driver power is installed
  and reachable, and the operator knows how to use it without software; each
  endpoint reaches the intended physical device; wiring, grounds, driver and
  power settings, and physical operating limits match the connected hardware;
  and the required board revision and flashed firmware build are correct where
  Joshua cannot report them;
- how to run, stop or cancel the operation, inspect diagnostics, and clean up,
  including when to use the physical disconnect instead of software.

The guide should mark each step Joshua cannot perform or verify as a manual
operator step. For each real-motion run, the adapter must obtain confirmation
directly from the human responsible for the hardware through a channel the AI
model cannot supply or bypass. A model-supplied tool argument or automatic app
approval does not count. If the operator cannot confirm a required item, the
guide must tell them not to proceed, and the adapter must not begin motion.
Passing configuration checks does not replace human confirmation.

## MCP constraints

Before MCP exposes an operation, the implementing subsystem must define and test
its input, output, state transition, cancellation behavior, errors, observability,
and operational limits. MCP integration should adapt that interface instead of
creating a separate runtime state machine.

The MCP front end should use each subsystem's documented runtime interface,
including ROS 2 topics, services, or actions when they are the intended
integration boundary. It must not communicate directly with hardware drivers
or bypass the existing node graph. Its authorization flow must not bypass the
applicable config validation, operation-specific limits, or approval from the
operator responsible for the hardware. These mechanisms are not a general
guarantee of hardware safety. Cancellation, timeouts, partial failure,
diagnostics, and cleanup should remain observable.

Joshua should continue to work without MCP through its protobuf config and
normal launcher.

## Contributing

Human contributors should follow [`CONTRIBUTING.md`](../CONTRIBUTING.md).
AI-assisted workflows must also follow [`AGENTS.md`](../AGENTS.md). Each pull
request should:

- remain focused, coherent, and independently useful;
- explain context, purpose, scope, rationale, evidence, limits, and likely
  follow-up work in plain language;
- update affected subsystem documentation when merged support changes;
- distinguish software, replay where available, simulation, and hardware
  evidence.

Real-device runs require explicit approval from the operator responsible for
the hardware and confirmation that the setup is ready for that run.
