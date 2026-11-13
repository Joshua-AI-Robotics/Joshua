# Coding guidelines

Joshua should remain easy to understand, extend, and maintain as it grows.
These guidelines apply to all contributions, including AI-assisted work.
AI makes adding code easy; contributors remain responsible for understanding
and owning its behavior and architecture.

## Required reading

At the start of each implementation or review task, agents must open this guide
and the applicable directory instructions before planning or changing code.
Read the affected subsystem's README as well. Re-read guidance for a new task
or after instructions change.

| Directory | Local rules |
| --- | --- |
| `ros2/` | [Component nodes and binary entrypoints](../ros2/AGENTS.md) |
| `robot/` | [Modular implementations and factory boundaries](../robot/AGENTS.md) |

The root [AGENTS.md](../AGENTS.md) and its tool bridges route agents here.
A Markdown link is not an automatic import. Documentation checks validate
links and bridges; contributors and reviewers remain responsible for checking
that implementations follow the rules.

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
