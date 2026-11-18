# Developer tools

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read the local
[README.md](README.md) before editing. This file owns the rules and architecture
contract for `tools/`; child directories inherit it unless they add local rules.

## Architecture contract

Tools adapt existing subsystem contracts; production runtime must not depend
on these utilities. Use protobuf configuration and factory-owned discovery
metadata rather than a parallel device registry, validation implementation,
message bus, or lifecycle manager. Recording tools consume ROS data and preserve
source metadata, validity, loss, and clock domains.

Use the nearest existing utility before introducing a wrapper. Keep model lock
regeneration reproducible and separate from runtime loading. Python bench drivers
stay here rather than in the C++ robot layer. A tool capable of device I/O or
motion follows the root hardware authorization rule.

The [MCP contract](mcp/AGENTS.md) owns read-only adapter boundaries. Test changed
tools with their existing Bazel targets in Docker; use synthetic ROS messages,
fake helpers, and temporary directories. Never substitute a physical bench run
for automated verification.
