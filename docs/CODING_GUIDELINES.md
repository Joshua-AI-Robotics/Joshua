# Coding guidelines

Joshua should remain easy to understand, extend, and maintain as it grows.
These guidelines apply to all contributions, including AI-assisted work.
AI makes adding code easy; contributors remain responsible for understanding
and owning its behavior and architecture.

## Where rules live

Open this guide and the applicable `AGENTS.md` files before implementation or
review. Inspect the existing implementations, factories,
callers, and tests before proposing another abstraction. A link alone does not
load its target into an agent's context.

This guide owns shared design and review principles. The root
[AGENTS.md](../AGENTS.md) owns repository operations and hardware authorization.
Each directory's `AGENTS.md` owns its purpose, architecture, dependency rules,
lifecycle, usage/examples, and verification guidance. Tool-specific settings
point to the canonical instructions.
The [architecture map](ARCHITECTURE.md#directory-contracts) links the local
contracts. Change a rule at its owner rather than copying it into several files.

## Design principles

1. **Give each responsibility a clear owner.** Make resource ownership, state,
   initialization, recovery, and shutdown easy to locate. Keep one source of
   truth for each fact and one owner for each lifecycle. Tools and consumers
   should use the owning component's contract rather than duplicate its logic
   or independently manage its resources.

2. **Use the fewest layers needed.** Extend the responsible component first;
   share focused functions or internal helpers where useful. A new wrapper,
   manager, framework, or service must solve a concrete problem and make the
   overall design easier to understand. Forwarding calls or accommodating one
   tool alone does not justify another layer. Preserve boundaries that clarify
   ownership instead of combining unrelated responsibilities into one component.

3. **Keep core behavior easy to trace.** A reader should be able to follow
   configuration → owning component → execution and data flow. Keep
   sensor-specific behavior in its driver or ROS 2 node, and factories focused
   on selecting and constructing configured implementations. Share acquisition,
   health, or metadata helpers within those boundaries; do not replace meaningful
   sensor nodes with a generic node merely to reuse code. Keep dependencies and
   failure paths explicit.

4. **Build on ROS 2 for tools and data pipelines.** Joshua extends ROS 2. Reuse
   its topics, services, actions, QoS, introspection, diagnostics, logging, and
   rosbag2 recording where they meet the requirement. External tools should
   adapt established interfaces through thin adapters. Avoid parallel message
   buses, monitoring systems, lifecycle owners, and recording pipelines. When a
   custom path is necessary, explain the specific gap in existing capabilities
   and keep the addition limited to that gap.

5. **Scale through cohesive components and stable interfaces.** Build for
   demonstrated requirements and extend existing contracts where appropriate.
   Keep unrelated sensor, transport, and tool behavior independently changeable.
   Avoid speculative extension frameworks and scattered special cases.
   Protobuf configuration remains the source of truth for robot setup. Prefer
   changes that a future contributor can explain, debug, test, and remove
   without tracing unrelated subsystems.

## Implementation and review

Trace the affected subsystem's existing ownership and interfaces. When adding
or changing a component, layer, or pipeline, explain in the implementation or PR
summary:

- Which component owns the resources, state, and lifecycle?
- Why is this addition needed, and can an existing component or ROS 2 capability
  meet the requirement?
- How does the change preserve a direct, understandable execution and data path?
- How was the behavior verified, and what limitations remain?

Working code and passing tests are necessary; architectural clarity is also a
review requirement. Contributors must be able to explain the code they submit,
including code produced with AI.

## Compatibility and evidence

Preserve multiple configured devices, actuators, sensors, and topics. A refactor
must preserve public APIs, protobuf meaning, topic names/payloads, units, clock
domains, defaults, initialization, and shutdown. An intentional behavior change
must identify the baseline, migration, affected callers/presets, and tests.
Preserve protobuf field and enum identities; reserve removed names and numbers.

Use focused unit and contract tests for changed behavior, then integration tests
for actual node/topic/service interactions. Prefer controlled clocks, fake
transports, and PTYs over physical devices for repeatable failures. Exercise
failure handling, not only successful initialization. Report exact commands,
results, environment overrides, and uncovered behavior. Passing simulation or
protocol tests does not establish physical readiness, stopping, or timing.
Canonical test services and lint commands live in the root instructions and
[CONTRIBUTING.md](../CONTRIBUTING.md#testing-and-ci).

## Comments

Keep production-code comments minimal. Remove redundant descriptions, stale
explanations, commented-out code, and comments that justify leaving a workaround
in place when touching that code. Prefer clear names and direct control flow.
Keep license/generated notices and essential API, wire-protocol, safety, timing,
unit, and concurrency invariants. Preserve protobuf field semantics and hardware
limitations. Link TODO/FIXME items to an issue; do not replace necessary context
with an unexplained issue number. Broader legacy cleanup belongs in focused changes.

## Make recurring corrections enforceable

When review finds a recurring failure, first improve the owning API or code
structure, then add a compiler/type/lint restriction or a behavioral regression
test where practical. Prose communicates intent; human review resolves design
judgments that those checks cannot decide. Every new checker needs a known-good
case and a deliberately invalid case that demonstrates rejection.

Current CI checks agent instruction links, skill metadata, CODEOWNERS coverage,
Docker entrypoint contracts, and Bazel tests in Humble and Jazzy. The existing
transport boundary target checks public compilation contexts for leaked concrete
transport headers. These checks do not establish compliance with every rule:
repository-wide static lint, a complete dependency guard with negative fixtures,
changed-comment checks, isolated test-service device mounts, and a representative
full-graph E2E smoke test are not all enforced today. PR authors must identify
relevant gaps instead of reporting an unimplemented check as passed. CODEOWNERS
coverage does not itself require owner approval or configure branch protection.
