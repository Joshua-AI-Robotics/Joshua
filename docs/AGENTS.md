# Documentation

Follow the repository [AGENTS.md](../AGENTS.md). This file owns documentation rules,
the guide index, and navigation into repository skills.

## Architecture contract

Each directory's `AGENTS.md` combines its purpose, architecture, dependencies,
implementation rules, usage/examples, safety limits, and verification commands.
Keep each fact in its owning file and link to it. Create a local file when a
subsystem has distinct context; smaller folders inherit their ancestor guidance.

Root `AGENTS.md` owns global engineering and contribution rules. `ARCHITECTURE.md`
maps directory contracts and system data flow. Dedicated protocol contracts,
qualification evidence, and extended guides stay at their existing paths and
are linked from the owning `AGENTS.md`.

Preserve wire semantics, hardware evidence, and operator procedures. Distinguish
implemented behavior from proposed capabilities and physical claims from
software-only tests. Update the owning contract and focused PR description
instead of keeping planning documents or RFC archives in this directory.
Keep future delivery plans in issues and PRs; documentation describes active
contracts, operating procedures, implementation limits, and evidence.
The three legacy demo plans indexed below are temporary retention exceptions.
They are historical planning context, not active contracts, and are planned for
removal after any still-relevant work moves to issues/PRs.
Skills remain in `skills/` and route to these same contracts.

## Instruction files

Root instructions apply repository-wide. Local files add only their owned
context and are inherited by descendants. A deeper file must not silently
contradict a parent contract. Read applicable files even when an agent starts
at the repository root.

Keep all directory context in `AGENTS.md`. Tool-specific settings or existing
pointers may route to it but must not define separate engineering rules. Keep
the root Gemini context configuration pointed at `AGENTS.md`. Link new files
from the nearest ancestor's directory navigation and the architecture map when
adding a new ownership boundary. Avoid duplicate context across those files.

## Verification

Update the guide index below when a guide is added, moved, or removed. Check
local links and section anchors, `hooks/agents_doc_check.sh`, and
`hooks/skills_doc_check.sh` for changed instructions/skills. Documentation-only
changes need no robot run.

## Documentation

[Mixed serial qualification](SERIAL_COMM_QUALIFICATION.md) records the software
evidence workflow and the outstanding physical qualification gates.

| Topic | Guide |
|-------|--------|
| Install, Docker, first run, presets | [GETTING_STARTED.md](GETTING_STARTED.md) |
| Local MCP setup, AI IDE connection and recovery | [MCP_SETUP.md](MCP_SETUP.md) |
| System architecture, protobuf config, and data flow | [ARCHITECTURE.md](ARCHITECTURE.md) |
| ROS 2 code generation and single-source node implementation rules | [../ros2/AGENTS.md](../ros2/AGENTS.md) |
| Global coding and contribution rules | [Root AGENTS.md](../AGENTS.md#coding-guidelines) |
| Typed observation metadata and reader contract | [MCP AGENTS.md](../tools/mcp/AGENTS.md#typed-observation-contract) |
| Shared C++ robot clock, configuration, and integration checklist | [ROBOT_TIME.md](ROBOT_TIME.md) |
| AI stack (inference, models, deps) | [../ai/AGENTS.md](../ai/AGENTS.md) |
| Source-linked catalog of implemented components and representative presets | [SUPPORTED_COMPONENTS.md](SUPPORTED_COMPONENTS.md) |
| Supported hardware models, aliases, drivers, presets, and intended scope | [SUPPORTED_HARDWARE.md](SUPPORTED_HARDWARE.md) |
| Separate hardware test records, environments, measured results, and limitations | [TESTED_HARDWARE.md](TESTED_HARDWARE.md) |
| Shared JoshuaWire code, test layout and migration adapters | [../firmware/common/AGENTS.md](../firmware/common/AGENTS.md) |
| JoshuaWire serial validation procedure | [JOSHUA_WIRE_VALIDATION.md](JOSHUA_WIRE_VALIDATION.md) |
| Repository skills (agent + human playbooks) | [skills/AGENTS.md](skills/AGENTS.md) |
| JoshuaWire core/profile contract | [JOSHUA_WIRE.md](JOSHUA_WIRE.md) |
| AM243 EtherCAT bring-up | [am243_ethercat.md](am243_ethercat.md) |
| Setup scripts and builds | [scripts/AGENTS.md](../scripts/AGENTS.md) |
| Simulation (MuJoCo, Isaac Sim) | [simulation/AGENTS.md](../simulation/AGENTS.md) |
| Data collection (DataStore) | [ai/train/AGENTS.md](../ai/train/AGENTS.md) |
| React web UI | [ui/README.md](../ui/README.md) |

## Temporary demo plans — planned for removal

These documents were moved from `doc/` to keep documentation under `docs/`.
Their implementation and hardware claims have not been revalidated. Migrate
still-relevant work to issues/PRs, keep implemented contracts with their owners,
then remove these documents and their index entries.

- [Camera-head and R2S2R demo plan](CAMERA_HEAD_DEMO.md)
- [OpenAI Decisions API camera-head demo plan](OPENAI_DECISIONS_CAMERA_HEAD_DEMO.md)
- [Robot nervous-system demo plan](ROBOT_NERVOUS_SYSTEM_DEMO.md)

## Directory navigation

- [skills/](skills/AGENTS.md)
