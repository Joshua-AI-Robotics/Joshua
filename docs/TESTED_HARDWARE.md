# Hardware validation history

Validation history now lives inside each [device file](hardware_catalog/),
with setup, specification, measurements, evidence, tester and reviewer attribution.
See [catalog contribution guidance](hardware_catalog/AGENTS.md).

MCP: `query_hardware_catalog(view="history")` queries the history.
The original `list_hardware_test_records` compatibility API remains available.
