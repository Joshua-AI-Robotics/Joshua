# Documentation

[Mixed serial qualification](SERIAL_COMM_QUALIFICATION.md) records the software
evidence workflow and the outstanding physical qualification gates.

| Topic | Guide |
|-------|--------|
| Install, Docker, first run, presets | [GETTING_STARTED.md](GETTING_STARTED.md) |
| Local MCP setup, AI IDE connection and recovery | [MCP_SETUP.md](MCP_SETUP.md) |
| System architecture, protobuf config, and data flow | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Production cleanup options for perception acquisition and ROS node ownership | [PRODUCTION_CODE_CLEANUP.md](PRODUCTION_CODE_CLEANUP.md) |
| ROS 2 code generation and single-source node implementation rules | [../ros2/AGENTS.md](../ros2/AGENTS.md) |
| Mixed-hardware demo and shared runtime, simulator, and evidence requirements | [../doc/ROBOT_NERVOUS_SYSTEM_DEMO.md](../doc/ROBOT_NERVOUS_SYSTEM_DEMO.md) |
| Camera-head setup, controller comparisons, and R2S2R training/transfer gates | [../doc/CAMERA_HEAD_DEMO.md](../doc/CAMERA_HEAD_DEMO.md) |
| OpenAI Decisions adapter, API failure handling, and demo gates | [../doc/OPENAI_DECISIONS_CAMERA_HEAD_DEMO.md](../doc/OPENAI_DECISIONS_CAMERA_HEAD_DEMO.md) |
| Shared C++ robot clock, configuration, and integration checklist | [ROBOT_TIME.md](ROBOT_TIME.md) |
| AI stack (inference, models, deps) | [../ai/README.md](../ai/README.md) |
| AI integration vision, requirements, user scenarios, checkpoints, feedback orchestration, and MCP constraints | [AI_INTEGRATION.md](AI_INTEGRATION.md) |
| Typed observation base contract, metadata and proposed adapters | [MCP_OBSERVATION_DESIGN.md](MCP_OBSERVATION_DESIGN.md) |
| Source-linked catalog of implemented components and representative presets | [SUPPORTED_COMPONENTS.md](SUPPORTED_COMPONENTS.md) |
| Supported hardware models, aliases, drivers, presets, and intended scope | [SUPPORTED_HARDWARE.md](SUPPORTED_HARDWARE.md) |
| Separate hardware test records, environments, measured results, and limitations | [TESTED_HARDWARE.md](TESTED_HARDWARE.md) |
| Board layer RFC (motor × board × transport) | [BOARD_LAYER_RFC.md](BOARD_LAYER_RFC.md) |
| Board/comm separation and JoshuaWire EtherCAT plan | [BOARD_COMM_SEPARATION_PLAN.md](BOARD_COMM_SEPARATION_PLAN.md) |
| Serial reliability, mixed sensor streaming/control, and implementation review gates | [SERIAL_COMM_IMPROVEMENT_PLAN.md](SERIAL_COMM_IMPROVEMENT_PLAN.md) |
| Shared JoshuaWire code, test layout and migration adapters | [../firmware/common/README.md](../firmware/common/README.md) |
| JoshuaWire serial validation procedure | [JOSHUA_WIRE_VALIDATION.md](JOSHUA_WIRE_VALIDATION.md) |
| Agent instructions RFC (AGENTS.md contract) | [AGENTS_RFC.md](AGENTS_RFC.md) |
| Repository skills (agent + human playbooks) | [skills/README.md](skills/README.md) |
| JoshuaWire core/profile contract | [JOSHUA_WIRE.md](JOSHUA_WIRE.md) |
| AM243 EtherCAT bring-up | [am243_ethercat.md](am243_ethercat.md) |
| Setup scripts and builds | [scripts/README.md](../scripts/README.md) |
| Simulation (MuJoCo, Isaac Sim) | [simulation/README.md](../simulation/README.md) |
| Data collection (DataStore) | [ai/train/README.md](../ai/train/README.md) |
| React web UI | [ui/README.md](../ui/README.md) |
