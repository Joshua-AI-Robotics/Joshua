# AI integration

Joshua connects its robot stack to model adapters, data collection,
development environments, validation, and contributor workflows. This guide
shows how those pieces fit and records the cross-cutting rules for extending
them.

The linked subsystem guides own their commands and interfaces. This page does
not replace them or track live delivery status.

## Start with the task

| Goal | Start here |
|---|---|
| Set up a development machine | [Getting started](GETTING_STARTED.md) |
| Understand how one config becomes a ROS 2 system | [Architecture](ARCHITECTURE.md) |
| Add or change an inference model | [AI stack](../ai/README.md) |
| Record and inspect robot data | [Data collection](../ai/train/README.md) |
| Run a simulation | [Simulation](../simulation/README.md) |
| Add or change a `.pbtxt` preset | [Config guide](../config/README.md) |
| Edit a config with a generated form | [Web control panel](../ui/README.md) |
| Build or test a change | [Contributing](../CONTRIBUTING.md#testing-and-ci) |
| Build a deployable package | [Scripts and builds](../scripts/README.md) |
| Use a coding agent in this repository | [Agent instructions](../AGENTS.md) |
| Use or author a repeatable workflow | [Joshua skills](skills/README.md) |

## Current contracts

| Area | Current source | Boundary |
|---|---|---|
| Configuration | Protobuf schemas, presets, and [validation](../config/README.md) | The `.pbtxt` config is the source of truth. The web UI imports, edits, and downloads configs in the browser; canonical semantic validation remains `config::ValidateConfig`. |
| AI inference | The [inference host and model adapters](../ai/README.md) | The host owns ROS 2 wiring, message decoding, scheduling, output publication, and operational-limit conversion. Adapters own model-specific loading, preprocessing, inference, and postprocessing in per-model environments. |
| Data collection | [DataStore](../ai/train/README.md) | DataStore records interleaved rosbag2 events and exports Hugging Face, JSONL, CSV, or Parquet data. Recording sessions are episode-indexed, but synchronized state-action training episodes are not produced. |
| Execution | The launcher, [node generator](../node_generator/README.md), ROS 2 nodes, and [simulation](../simulation/README.md) | The launcher selects the runtime path; NodeGenerator manages ROS 2 node processes; each subsystem owns its runtime interfaces. |
| Verification and safety | Targeted tests, Docker CI tasks, simulation, subsystem checks, and [hardware rules](../AGENTS.md) | Software results do not imply hardware validation. |
| Contributor workflows | Repository documentation, `AGENTS.md`, and [repo-owned skills](skills/README.md) | Skills document and sequence existing repository workflows; they do not define parallel build, validation, or launch paths. |

## Direction

Future integration work follows these rules:

1. **Protobuf remains the source of truth.** The UI consumes and produces
   Joshua configs today. Planned skills, generated inventories, and MCP tools
   should use the same schema; none should own a parallel robot schema.
2. **Subsystems own runtime contracts.** Cross-cutting tools should expose a
   runtime capability only after the subsystem that owns its behavior has
   defined, documented, and tested it.
3. **Current support requires merged evidence.** Open pull requests can inform
   planning, but they do not establish a supported capability.
4. **Evidence has levels.** Static checks, container tests, recorded-data or
   trajectory replay where available, simulation, and hardware runs support
   different claims.
5. **Hardware runs remain deliberate.** A generated config, successful
   validator, simulation result, or agent recommendation does not authorize a
   hardware run.
6. **Each pull request should be independently useful.** One change should be
   understandable and reviewable without accepting later work.

## Work tracks

| Track | Sequence | Review boundary |
|---|---|---|
| Contributor workflows | supported-component catalog → validation → configuration → guided integration | Subsystem owners review layer-specific guidance. |
| Runtime and MCP | typed runtime capability → optional MCP front end and operator guide | Runtime owners define and test capabilities before MCP exposes them. |

The tracks can advance independently. A unit waits only for the prerequisites
listed below.

## Readiness dependencies

| Proposed follow-up | Ready when | Boundary |
|---|---|---|
| Supported-component catalog | Its source files and subsystem reviewers are identified. | List only merged boards, communication capabilities, perceptions, models, simulations, ROS 2 data types, and representative presets. |
| Change-validation skill | The catalog and relevant test commands are stable. | Select existing checks and state what each result proves; do not implement another validator. |
| Configuration skill | The validation skill and canonical config validator are stable. | Create or modify presets through existing schemas and validation paths. |
| Layer-specific guidance | That layer's extension contract is merged and documented. | Cover communication, board/GPIO, and perception separately because their owners and evidence differ. |
| Guided-integration skill | The catalog, validation skill, and configuration skill are available. | Compose supported components into a preset; route new-driver work to the owning layer. |
| MCP front end | At least one typed runtime capability and its tests are stable. | Adapt Joshua's runtime and config interfaces; do not control hardware drivers or raw ROS topics directly. |

This table records architectural readiness, not delivery status. Progress
belongs in GitHub, and each pull request owns its detailed acceptance criteria.

## Contributor workflow boundaries

The planned supported-component catalog should distinguish merged support from
experimental and planned work. Every supported entry should point to
implementation, configuration, tests, and system constraints where applicable.
Facts that can be derived from schemas, manifests, BUILD targets, or presets
should not be copied by hand.

The planned validation skill should use commands owned by subsystem
documentation and [`CONTRIBUTING.md`](../CONTRIBUTING.md). It should
distinguish documentation checks, targeted tests, CI-equivalent containers,
recorded-data or trajectory replay where available, simulation, and hardware
evidence, and state what was skipped or unavailable.

The planned configuration skill should start from the nearest merged preset.
The web UI may help edit a `.pbtxt` file, but semantic validation still uses
Joshua's canonical validator. Device paths, network interfaces, camera indices,
and operation mode must be inspected before a run is proposed.

The first guided-integration workflow should assemble a preset from supported
components, validate it, and prepare review evidence. Adding a transport, board,
GPIO interface, sensor, or other runtime extension should remain a separate,
owner-reviewed change.

## Runtime and MCP boundaries

A future MCP integration should be an optional front end over Joshua's typed
capabilities. Before it exposes an operation, the owning runtime should define
the operation's input, output, state transition, cancellation behavior, errors,
observability, and safety boundary. Integration work should not prescribe a new
runtime state machine.

The MCP front end should call runtime and configuration interfaces rather than
raw ROS topics or hardware drivers. The MCP host should control user
authorization, while Joshua's runtime should continue enforcing hardware safety.
Cancellation, timeouts, partial failure, diagnostics, and cleanup should remain
observable.

A later operator guide should cover configuration, selecting or building the
binary, installation, connection, starting and stopping a Joshua-controlled
operation, status, diagnostics, and server hosting. Joshua should continue to
work without MCP through its protobuf config and normal launcher.

## Pull-request boundaries

All work follows [`CONTRIBUTING.md`](../CONTRIBUTING.md) and
[`AGENTS.md`](../AGENTS.md). Changes from this direction also:

- keep one architectural unit per pull request;
- explain context, purpose, scope, rationale, evidence, limits, and likely
  follow-up work in plain language;
- update the owning subsystem guide when merged support changes;
- distinguish software, replay where available, simulation, and hardware
  evidence;
- avoid real-device runs unless the user authorizes them for the current turn
  and confirms the hardware setup.

## Non-goals

- A second robot configuration language.
- A second build, test, launch, release, or hardware-control path.
- Automatic hardware execution from a skill, UI, or MCP prompt.
- Defining runtime interfaces outside their owning subsystem.
- Compatibility claims based only on architectural resemblance.

## Maintaining this guide

Update this guide only when current support, a cross-cutting rule, ownership
boundary, dependency, or non-goal changes. Track delivery in GitHub and put
implementation details in the pull request that owns them. Describe a
capability as current only after it merges.
