---
name: joshua-guided-integration
description: Guide an AI IDE through configuring existing Joshua hardware or implementing a new component, using MCP inspection and normal workspace edits.
---

# Guided hardware integration

Check the Joshua MCP connection and call `list_supported_components`. If
unavailable, explain the problem and prompt for `./scripts/joshua_mcp.sh setup`
or reconnection; use [the setup guide](../../MCP_SETUP.md) for recovery. Do not
claim a connection based only on configuration being present.

Ask which hardware the user has and what they want it to do, then inspect the
workspace and existing presets before asking for additional facts. Keep the
[protobuf configuration](../../../config/README.md) authoritative. Catalog
metadata describes compiled software, not the attached hardware; a missing
catalog entry alone does not establish incompatibility.

- **Existing implementation:** reuse the closest compatible preset and driver.
  Gather missing device identifiers, wiring and configuration from the user and
  subsystem documentation. Use normal IDE workspace edits to update the actual
  `.pbtxt`, then call `validate_config`, `list_devices` and `describe_device`
  against that file beneath the prepared config root. Review errors and changes
  with the user. Hand off physical connection and runtime checks using the
  subsystem's documented procedure.
- **New component:** first check whether an existing board, transport, motor or
  sensor implementation can be reused. Read the relevant
  [robot subsystem](../../../robot/README.md) contract and nearby tests. Use IDE
  tools for code, build/factory registration, configuration, tests and docs;
  change protobuf only when its existing fields cannot describe the component.
  Run the supported Docker checks. Re-run `./scripts/joshua_mcp.sh prepare`,
  reconnect MCP and inspect again so results reflect the changed implementation.
  Report candidate support and remaining verification explicitly.

MCP is read-only. Never substitute a generic shell, file-write, publish or robot
lifecycle tool for these workflows. Follow the current hardware confirmation
rules in [AGENTS.md](../../../AGENTS.md) before any physical launch; firmware
flashing remains a separate human-initiated operation. Finish with the edited
files, software checks, refreshed inspector revision and remaining operator
steps. A valid preset or passing test does not establish hardware readiness.
