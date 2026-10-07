# AI integration

## Vision

Joshua's purpose is to help people apply robotics to work they already
understand, without requiring robotics expertise. These principles guide its
development:

- **Use chat to express intent.** Let users describe and refine their goals
  using their own domain knowledge.
- **Keep complexity in Joshua.** The binary and backend own software
  preparation, configuration, device access, and execution. Guide users through
  the physical steps that remain theirs.
- **Build reusable capabilities.** Integrate hardware through tested contracts
  so supported actions can be composed, repeated, and adapted across tasks.
- **Verify real outcomes.** Establish task success through measured robot
  feedback and make progress, failures, and missing evidence visible.
- **Keep the operator in control.** Require direct human readiness confirmation
  before real motion and provide stop controls that remain usable without the
  model.
- **Ship useful increments.** Prove one complete workflow, then expand toward
  100% coverage of implemented hardware paths.

## Requirements

- **Joshua owns preparation and execution.** Chat gathers and refines intent;
  the binary and backend handle software setup, configuration, device access,
  and execution. MCP wraps tested subsystem operations.
- **Use the existing robot configuration.** Hardware identity, wiring,
  calibration, and limits come from protobuf configuration. Capability claims
  match the installed version and merged, verified implementations.
- **Make every task explicit and bounded.** Resolve the target, movement,
  units, speed, repetition count, and any variations into a validated plan.
- **Get direct operator confirmation.** Before real motion, the person
  responsible for the hardware confirms readiness through a channel the model
  cannot supply or bypass. Approval covers the complete bounded plan; changes
  or restarts require fresh confirmation.
- **Show what actually happened.** Use correctly associated, fresh measured
  feedback with known units, reference frame or zero, and tolerances. Report
  progress, completed actions, timeouts, and faults. Label command or step-count
  estimates separately from measured position. Missing feedback or a fault
  invokes the task's defined stop behavior; cancellation remains available
  without the model.
- **Demonstrate hardware coverage.** Each implemented hardware path needs
  applicable MCP integration and verification. Record missing evidence as a gap
  rather than presenting that path as covered.

Early checkpoints can use a prepared, supported environment. A complete
fresh-host installation journey remains a separate release gate.

## Interface

Start with an existing AI IDE or compatible chat application connected through
MCP. A dedicated Joshua chat interface can follow if needed.

**User → Chat → MCP → Joshua backend → Robot**

Connecting supported hardware should be guided. Adding new hardware requires
implementation, testing, documentation, and release through the relevant
subsystem.

## User scenarios

These scenarios describe the intended experience as the checkpoints ship.

### Set up hardware

“Help me set up this arm.” Joshua should prepare the software and configuration,
guide the user through physical connections and required checks, and explain
which actions are available. If the hardware type is not implemented, Joshua
should start a guided integration wizard through chat. It gathers hardware
details, identifies reusable components and missing support, and walks through
configuration, implementation, testing, and documentation. The wizard shows
progress and required human steps. Its outcome is a validated configuration
using implemented components, a reusable existing setup, or a clear list of
missing integration steps. Configuration validation checks integrity; physical
readiness still needs verification and operator confirmation. New hardware needs
verification and release before Joshua presents it as supported.

### Connect to an existing robot

“Connect to Arm A and run my saved movement.” Joshua should reuse the robot's
configuration, check its current connection and capabilities, and ask for any
missing or changed details. It previews the resolved task, obtains fresh
operator readiness confirmation before motion, and shows progress and stop
controls. The user should not need to repeat software setup for each session.

### Conduct an experiment

“Repeat the saved movement 100 times per speed at two supported speeds and
compare how closely the joint reaches its targets.” The preview specifies
**100 cycles per speed, 200 cycles total**, with the movement, endpoints, speeds,
measurement, and limits resolved before operator approval.

The experiment can be saved as a reusable task or script referencing the robot's
protobuf configuration. An optional skill can guide creation and refinement
through chat; Joshua validates and executes the complete approved plan. Results
include requested and completed cycles, interruptions, partial results, and
measured joint endpoint error with its units, reference frame or zero, and
tolerance. Tool-position accuracy requires its own measurement. If the required
feedback is unavailable, Joshua should explain what is missing. The task format
and runtime interfaces remain follow-up design decisions.

## Checkpoints

Ship the first three checkpoints using one supported environment and robot.
Expand hardware coverage alongside them.

### 1. Connect and inspect through MCP

A user connects an AI IDE or chat client and asks, “What can this setup do, and
is my configuration valid?” Joshua reports its installed version, configured
devices, supported capabilities, and validation errors without opening devices.
This establishes a working client connection and inspection flow. Live
connection probes are separate approved backend steps that respect device
ownership.

### 2. Execute one verified robot task

The user requests one supported movement on a named robot. For example, the
preview defines a base-joint cycle as **0° → 45° → 0°**, at a supported speed
and within that arm's calibrated limits. After direct operator confirmation,
Joshua executes it, shows progress, and uses physical feedback to verify each
endpoint against a declared reference and tolerance.

The first demonstration combines read-only inspection, independent operator
confirmation, and that bounded movement. It covers success, timeout,
cancellation, and fault handling, with stop controls usable without the model
and observable backend stop status. Verify stop behaviour separately from
cancellation acknowledgments. Reconnecting and repeating the task should reuse
the saved setup without repeating software preparation.

### 3. Compose, repeat, and vary actions through chat

The user saves that movement as a reusable task or script, optionally guided by
a skill, and later refines its count or speed through chat. They can group
supported movements and repeat the group with finite parameter variations. The
LLM clarifies the request and previews the full plan; the backend validates and
executes it as one bounded, operator-approved run. Results retain requested and
completed actions, cycles, variations, and measured outcomes, including partial
results after interruption. Start with one useful sequence before considering a
dedicated experiment framework.

### 4. Reach 100% hardware coverage

The same inspection and applicable operation/feedback flows work across every
implemented board, transport, actuator, and sensor path in the runtime version
under review. Each path has recorded demonstration and verification evidence;
missing or unverified paths keep this checkpoint open. New hardware can merge
first, with its MCP integration following the relevant merged contract in the
same or a linked PR. Track that gap until integration and verification land.

Agree on the first robot, task, client, and feedback contract before writing
implementation designs in follow-up PRs.

## Relevant current capabilities

Joshua already connects protobuf configuration, ROS 2 runtime components,
model adapters, data collection, simulation, and contributor workflows. The
current implementations and example presets are indexed in the
[supported-component catalog](SUPPORTED_COMPONENTS.md).

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

The proposed first motion-capable Model Context Protocol (MCP) release is an
optional front end for exactly one tested, bounded robot operation. AI inference,
including the existing [inference host](../ai/README.md), and
[data collection](../ai/train/README.md) are outside its scope.

Any contributor may propose these changes. Reviews should include people
familiar with the affected implementation; this does not create exclusive
subsystem or runtime roles.

| Change | Start from | Intended result |
|---|---|---|
| Change-validation skill | Existing subsystem documentation and test commands; use the catalog when available | A workflow that selects relevant checks and states what each result proves, without implementing another validator. |
| Configuration skill | Existing presets, schemas, and `config::ValidateConfig` | A workflow that starts from the nearest merged preset, modifies it through existing config paths, and validates the result without launching it. |
| Layer-specific guidance | A merged and documented extension contract | Separate guidance for communication, board/GPIO, and perception because their implementations and evidence differ. |
| Guided-integration skill | Existing merged components, presets, and validation paths; use the proposed skills when available | A workflow that composes supported components into a preset. New drivers and runtime extensions remain separate changes. |
| MCP front end and operator guide | A candidate bounded robot operation to define and test; [PR #90](https://github.com/Joshua-AI-Robotics/Joshua/pull/90) is an experimental reference | Start with read-only inspection; then stabilize one tested subsystem operation and add its motion-capable adapter and guide. |

These items describe independent proposed work, not current support or required
project phases. MCP requires only the tested contract and safeguards relevant
to each operation it exposes; it does not depend on the contributor tooling
items in this table.

PR #90 is an unmerged prototype for one actuator. Its author reports operating
Teensy/TB6600 hardware through ChatGPT and a local MCP server. The
[prototype guide](https://github.com/Joshua-AI-Robotics/Joshua/blob/f0c421f869e16ee85240d9573849f465389a6e78/mhs/README.md)
describes emitted-step position estimates and readiness flags supplied through
tool arguments. Independent human confirmation and measured physical feedback
remain necessary for this guide's first verified task. The prototype informs
follow-up work without establishing merged MCP support or selecting the first
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
