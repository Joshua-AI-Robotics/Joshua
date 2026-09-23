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
| Configuration | Protobuf schemas, presets, and [validation](../config/README.md) | The `.pbtxt` config is the source of truth. The web UI edits files but does not replace semantic validation. |
| AI inference | The [inference host and model adapters](../ai/README.md) | The host owns ROS 2 wiring; adapters own model loading, preprocessing, inference, and postprocessing in isolated environments. |
| Data collection | [DataStore](../ai/train/README.md) | Raw rosbag2 events and general-purpose exports exist today; synchronized training episodes do not. |
| Execution | The launcher, [node generator](../node_generator/README.md), ROS 2 nodes, and [simulation](../simulation/README.md) | Runtime interfaces remain owned by their subsystems. |
| Verification and safety | Targeted tests, Docker CI tasks, simulation, subsystem checks, and [hardware rules](../AGENTS.md) | Software results do not imply hardware validation. |
| Contributor workflows | Repository documentation, `AGENTS.md`, and [repo-owned skills](skills/README.md) | Skills order existing commands; they do not create another build, validation, or launch path. |

## Direction

Future integration work follows these rules:

1. **Protobuf remains the source of truth.** Skills, the UI, generated
   inventories, and MCP tools consume or produce Joshua configs; none owns a
   parallel robot schema.
2. **Subsystems own runtime contracts.** Board, communication, perception,
   ROS 2, inference, and MCP owners define their interfaces and tests.
3. **Current support requires merged evidence.** Open pull requests can inform
   planning, but they do not establish a supported capability.
4. **Evidence has levels.** Static checks, container tests, replay, simulation,
   and hardware runs support different claims.
5. **Raw evidence remains available.** Derived datasets and evaluation records
   identify their source data and conversion process.
6. **Hardware remains deliberate.** A generated config, successful validator,
   simulation result, or agent recommendation never authorizes a hardware run.
7. **Each pull request is independently useful.** One change should be
   understandable and reviewable without accepting later work.

## Work tracks

| Track | Sequence | Ownership |
|---|---|---|
| Contributor workflows | support catalog → validation → configuration → guided integration | AI maintainers, with subsystem-owner review for layer-specific guidance |
| Robot learning | raw recording → synchronized episode → model and evaluation identity | AI and data-path owners |
| Runtime and MCP | typed runtime capability → optional MCP front end and operator guide | Runtime and MCP owners |

The tracks can advance independently. A unit waits only for the prerequisites
listed below.

## Readiness dependencies

| Unit | Ready when | Boundary |
|---|---|---|
| Supported-capability catalog | This integration guide is merged. | List only merged boards, communication capabilities, perceptions, models, simulations, ROS 2 data types, and representative presets. |
| Change-validation skill | The catalog and relevant test commands are stable. | Select existing checks and state what each result proves; do not implement another validator. |
| Configuration skill | The validation skill and canonical config validator are stable. | Create or modify presets through existing schemas and validation paths. |
| Layer-specific guidance | That layer's extension contract is merged and documented. | Cover communication, board/GPIO, and perception separately because their owners and evidence differ. |
| Guided-integration skill | The catalog, validation skill, and configuration skill are available. | Compose supported components into a preset; route new-driver work to the owning layer. |
| Episode conversion | A concrete consumer and version are selected, with a synthetic golden recording. | Convert raw rosbag2 data deterministically without introducing a Joshua training framework. |
| Model and evaluation identity | Episode identity and one real training/evaluation path exist. | Add only the artifact and evidence fields required by that workflow. |
| MCP front end | At least one typed runtime capability and its tests are stable. | Adapt Joshua's runtime and config interfaces; do not control hardware drivers or raw ROS topics directly. |

This table records architectural readiness, not delivery status. Progress
belongs in GitHub, and each pull request owns its detailed acceptance criteria.

## Contributor workflow boundaries

The capability catalog defines `supported`, `experimental`, and `planned`.
Every supported entry points to implementation, configuration, tests, and
system constraints where applicable. Facts that can be derived from schemas,
manifests, BUILD targets, or presets should not be copied by hand.

The validation skill uses commands owned by subsystem documentation and
[`CONTRIBUTING.md`](../CONTRIBUTING.md). It distinguishes documentation
checks, targeted tests, CI-equivalent containers, replay, simulation, and
hardware evidence, and states what was skipped or unavailable.

The configuration skill starts from the nearest merged preset. The web UI may
help edit a `.pbtxt` file, but semantic validation still uses Joshua's
canonical validator. Device paths, network interfaces, camera indices, and
operation mode must be inspected before a run is proposed.

The first guided-integration workflow assembles a preset from supported
components, validates it, and prepares review evidence. Adding a transport,
board, GPIO interface, sensor, or other runtime extension remains a separate,
owner-reviewed change.

## Robot-learning boundaries

Raw recordings remain unchanged and independently inspectable. Episode
conversion records the source bag, converter and format versions, topic and
schema mapping, clock domains, episode boundaries, sampling frequency,
alignment policy, and deterministic split rules. Missing, duplicate, late, and
out-of-order samples have defined behavior, and identical inputs and options
produce identical aligned data and metadata.

Model identity follows one working training and evaluation path. It records an
immutable checkpoint identity, dataset and conversion identity, preprocessing
contract, locked environment, and reproducible evaluation command and result.
Offline, simulation, and closed-loop hardware evidence remain distinct.

Training orchestration, experiment dashboards, and a general model registry are
out of scope until repeated project needs justify them.

## Runtime and MCP boundaries

MCP is an optional front end over Joshua's typed capabilities. Before it exposes
an operation, the owning runtime defines the operation's input, output, state
transition, cancellation behavior, errors, observability, and safety boundary.
Integration work does not prescribe a new runtime state machine.

The MCP front end calls runtime and configuration interfaces rather than raw ROS
topics or hardware drivers. Authorization and hardware-sensitive operations
remain host-controlled; cancellation, timeouts, partial failure, logs, and
cleanup remain observable.

Its operator guide covers configuration, selecting or building the binary,
installation, connection, session start and stop, running Joshua, status, and
server hosting. Joshua continues to work without MCP through its protobuf config
and normal launcher.

## Pull-request boundaries

All work follows [`CONTRIBUTING.md`](../CONTRIBUTING.md) and
[`AGENTS.md`](../AGENTS.md). Changes from this direction also:

- keep one architectural unit per pull request;
- explain context, purpose, scope, rationale, evidence, limits, and likely
  follow-up work in plain language;
- update this guide and the capability catalog when merged support changes;
- distinguish software, replay, simulation, and hardware evidence;
- avoid real-device runs unless the user authorizes them for the current turn
  and confirms the hardware setup.

## Non-goals

- A second robot configuration language.
- A second build, test, launch, release, or hardware-control path.
- A universal training framework before Joshua supports one real workflow.
- A general experiment-tracking service or model registry.
- Automatic hardware execution from a skill, UI, or MCP prompt.
- Defining runtime interfaces outside their owning subsystem.
- Compatibility claims based only on architectural resemblance.

## Maintaining this guide

Update this guide only when current support, a cross-cutting rule, ownership
boundary, dependency, or non-goal changes. Track delivery in GitHub and put
implementation details in the pull request that owns them. Mark capabilities
current only after they merge.
