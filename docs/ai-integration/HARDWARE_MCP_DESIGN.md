# Hardware and MCP integration design

**Status:** Proposed design for discussion
**Checkpoints:** 1. MCP inspection; 2. First verified task; 4. 100% hardware coverage

## Goal

A user first connects an MCP-compatible chat client and inspects the installed
runtime, configuration, and capabilities without opening devices. The next
checkpoint invokes one tested robot operation with measured feedback and
operator controls. Additional hardware follows the same evidence requirements.

Checkpoint 2 proves the first complete operation path. Checkpoint 4 requires
100% coverage of implemented board, transport, actuator,
and sensor paths in the runtime version under review. Every path needs its
applicable MCP integration and evidence; missing or unverified paths keep the
checkpoint open.

## Proposed design

- Derive device identity, configuration, and installed-version support from
  canonical schemas and implemented contracts. Declared support, observed
  connection, and physical verification are separate results. Checkpoint 1 uses
  configuration facts, reported runtime state, and OS enumeration that opens no
  devices. Live connection probes are separate approved backend steps that
  respect device ownership.
- Keep the MCP adapter thin. Use the subsystem's intended runtime interface;
  device access, limits, cancellation, and lifecycle belong to the backend.
- Assign ownership of runtime startup and configuration changes. Report the
  active configuration and reject a task prepared for a different runtime.
  A manually prepared runtime can serve the first proof; automated preparation
  remains an explicit product gap.
- Define a direct operator confirmation path that model arguments cannot supply.
- Record applicable capabilities, feedback, tests, and physical verification
  for every implemented hardware path. Track new paths as open coverage gaps
  until integration and verification land against the relevant merged MCP
  contract, in the same or a linked PR. Hardware development can proceed before
  that follow-up; unverified paths do not count toward checkpoint 4.

## Suggested PR scopes

1. Bounded validation, installation/device inspection, and a read-only MCP
   connection walkthrough.
2. One subsystem operation with defined input, outcome, units, ownership,
   cancellation, faults, and required feedback.
3. Motion-capable MCP integration, trusted operator path, and a verified
   task walkthrough.
4. Additional hardware coverage and focused extension guides.

Use the current supported environment for the initial proof. Fresh-host setup
and broader operations retain separate acceptance requirements.

## Decisions for review

Which hardware, operation, and client come first? Who owns runtime preparation?
What can be observed, and what must the operator verify?

Related: [AI integration guide](../AI_INTEGRATION.md),
[configuration and validation](../../config/README.md),
and [observability design](OBSERVABILITY_DESIGN.md).
