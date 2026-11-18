# Hardware integration catalog

The [hardware catalog](hardware_catalog/) contains one authoritative JSON file per
device or board, including integration details and complete validation history.
See [catalog contribution guidance](hardware_catalog/AGENTS.md).

MCP: `query_hardware_catalog()` lists devices; `view="history"` reads their tests.
The original `list_supported_hardware` compatibility API remains available.
