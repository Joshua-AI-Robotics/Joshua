# Easy local MCP setup

One command prepares Joshua MCP, connects an AI IDE, and verifies read-only
inspection. Start with an existing checkout on x86-64 Ubuntu Linux. GPU hardware
is optional for this workflow; robot execution and specialized simulators retain
their own requirements.

## Dependencies

| Approach | Advantages | Tradeoffs |
|---|---|---|
| **Reuse Docker images** | Fastest path; encapsulates ROS, Bazel and libraries | Large initial downloads and builds |
| Smaller runtime image | Potentially smaller installation | More packaging and release testing |
| Native host installation | Direct use of host tooling | Version conflicts, privileged changes, unsupported development path |

Use the existing images with a dedicated CPU-only MCP service. The host supplies
Ubuntu, Docker, Compose, permissions and Python's standard library for the setup
wrapper. GPU workloads additionally need compatible host hardware and drivers.
Container Ubuntu versions do not define host-release compatibility.

## Two stacked changes

1. **Reliable launch.** Check the host and local Docker daemon, prepare the server
   and complete runfiles, then retain an immutable generation pinned to its image
   and daemon. `serve` uses that generation without compiling, installing or
   accessing hardware. Failed preparation preserves the prior generation.
2. **Guided setup.** `./scripts/joshua_mcp.sh setup` checks prerequisites, prepares
   MCP, verifies inspection, registers a compatible AI IDE and links the guided
   integration skill. Other clients receive standard stdio settings. Repeat setup
   resumes completed work and preserves conflicting registrations and policies.

Root documentation asks AI assistants to check MCP at session start and prompt
for setup when unavailable. For existing hardware, the IDE edits a protobuf
preset and MCP validates it. For a new component, the IDE extends the appropriate
subsystem, runs its tests, then refreshes the prepared inspector. MCP stays
read-only; validated configuration and candidate code do not establish physical
connectivity or readiness.

## Verification

Exercise both ROS container stacks, CPU-only launch, edited workspace inspection,
repeat setup, second-project registration, EOF/signals, reconnection and failure
recovery. Measure preparation time. Qualify host releases explicitly before
claiming support. Explain necessary host changes before requesting permission;
Docker access failures alone must never trigger a reinstall.
