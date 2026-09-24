# AI integration

Joshua already connects protobuf configuration, ROS 2 runtime components,
model adapters, data collection, simulation, and contributor workflows. This
guide records the shared rules for extending those pieces and independent
proposed follow-ups, including an optional Model Context Protocol (MCP) front
end.

Any contributor may propose these changes. Reviews should include people
familiar with the affected implementation; this does not create exclusive
subsystem or runtime roles.

## Current capabilities

| Area | Current implementation | Limitation |
|---|---|---|
| Configuration | Protobuf schemas, presets, and [`config::ValidateConfig`](../config/README.md) | The `.pbtxt` config is the source of truth. The [web UI](../ui/README.md) can import, edit, and download configs, but it only parses and formats them; pass its output through the applicable semantic validation path before use. |
| AI inference | The [inference host and model adapters](../ai/README.md) | The host handles ROS 2 wiring, message decoding, scheduling, output publication, and conversion of outputs marked `normalized` using configured actuator limits. Adapters handle model-specific loading, preprocessing, inference, and postprocessing in per-model environments. |
| Data collection | [DataStore](../ai/train/README.md) | DataStore records interleaved rosbag2 events and exports Hugging Face, JSONL, CSV, or Parquet data. Recording sessions are episode-indexed, but synchronized state-action training episodes are not produced. |
| Execution | The launcher, [node generator](../node_generator/README.md), ROS 2 nodes, and [simulation](../simulation/README.md) | The launcher selects the runtime path and NodeGenerator manages ROS 2 node processes. These interfaces are subsystem-specific; no general MCP-facing runtime contract is documented today. |
| Verification and safety | Targeted tests, Docker CI tasks, simulation, subsystem checks, and [hardware rules](../AGENTS.md) | Software results do not establish hardware validation. |
| Contributor workflows | Repository documentation, `AGENTS.md`, and [repository skills](skills/README.md) | Skills document and sequence existing workflows; they do not define parallel build, validation, or launch paths. |

## Design rules

1. **Protobuf remains the source of truth.** Planned skills, generated
   inventories, the UI, and MCP tools should use Joshua's existing schema
   instead of introducing another robot configuration language.
2. **Runtime contracts stay with their implementation.** A cross-cutting tool
   should expose an operation only after its behavior, inputs, outputs, errors,
   and tests are defined with the subsystem that implements it.
3. **Current support requires merged evidence.** Open pull requests can inform
   planning, but they do not establish a supported capability.
4. **Evidence has levels.** Static checks, container tests, recorded-data or
   trajectory replay where available, simulation, and hardware runs support
   different claims.
5. **Hardware runs remain deliberate.** Config validation, simulation, or an
   agent recommendation does not authorize a hardware run.
6. **Each pull request should be independently useful.** A change should be
   understandable and reviewable without accepting later work.

## Proposed follow-ups

| Change | Start from | Intended result |
|---|---|---|
| Supported-component catalog | Source files for merged boards, communication capabilities, perceptions, models, simulations, ROS 2 data types, and representative presets | A source-linked or generated view of current support, without copying facts that can be derived from schemas, manifests, BUILD targets, or presets. |
| Change-validation skill | The catalog and existing test commands | A workflow that selects relevant checks and states what each result proves, without implementing another validator. |
| Configuration skill | Existing presets, schemas, and `config::ValidateConfig` | A workflow that starts from the nearest merged preset, modifies it through existing config paths, and validates the result. |
| Layer-specific guidance | A merged and documented extension contract | Separate guidance for communication, board/GPIO, and perception because their implementations and evidence differ. |
| Guided-integration skill | The catalog, validation skill, and configuration skill | A workflow that composes supported components into a preset. New drivers and runtime extensions remain separate changes. |
| MCP front end and operator guide | One bounded Joshua operation with a tested interface | An optional adapter over existing runtime and configuration interfaces, plus setup, connection, operation, diagnostics, and hosting instructions. |

These items describe independent proposed work, not current support or required
project phases. MCP requires only the tested contract and safeguards relevant
to each operation it exposes; it does not depend on the contributor tooling
items in this table.

## MCP constraints

Before MCP exposes an operation, the implementing subsystem should define its
input, output, state transition, cancellation behavior, errors, observability,
and operational limits. MCP integration should adapt that interface instead of
creating a separate runtime state machine.

The MCP front end should call runtime and configuration interfaces rather than
raw ROS topics or hardware drivers. Its authorization flow must not bypass the
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

- cover one architectural unit;
- explain context, purpose, scope, rationale, evidence, limits, and likely
  follow-up work in plain language;
- update affected subsystem documentation when merged support changes;
- distinguish software, replay where available, simulation, and hardware
  evidence.

Real-device runs require explicit approval from the operator responsible for
the hardware and confirmation that the setup is ready for that run.
