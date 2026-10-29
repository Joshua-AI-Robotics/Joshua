# ROS 2 observability design

**Status:** Proposal; this PR adds documentation only.

## Principles

- **Evidence first:** include identity, configuration, timestamps, units, and freshness.
- **Honest results:** missing or stale evidence is `UNKNOWN`; observation does not certify safety.
- **Low token use:** compute checks continuously; give the LLM summaries and targeted details.
- **Replaceable storage:** one observability ABC, consistent behavior across providers.
- **Local control:** protobuf remains authoritative; controllers/drives own enforcement and stopping.

## Architecture

Observe whether machines match their expected setup, motion follows commands,
and measured values meet configured limits. Target **five-second freshness**;
roughly one second is desirable. Measure observation-to-MCP delay, excluding AI prose.

**Ingestion:** ROS feedback/logs → checks → buffered collector → storage.

**Queries:** AI client → MCP → `ObservabilityBackend(ABC)` → storage.

Reuse [ROS diagnostics](https://github.com/ros/diagnostics); initially export through
[Vector to VictoriaLogs](https://docs.victoriametrics.com/victorialogs/data-ingestion/vector/).
Check existing feedback at the appropriate rate, publish status around once per
second, and retain brief/recovered faults. Preserve device ownership, source
identity, and duplicate detection. Expected inventory comes from deployment and
[protobuf configuration](../config/README.md), including never-seen machines.

## Observability ABC

MCP validates requests and supplies authorization/query budgets. The selected
provider implements these asynchronous operations:

| Method | Input | Output |
|---|---|---|
| `health_summary` | Machine/robot scope, fault lookback | Expected/reporting counts, verdicts, freshness, top issues |
| `inspect_issue` | Machine/check, run/config, time window | Measurements, units, limits, and relevant events as of that window |
| `search_logs` | Scope/time, severity/tags, literal or regex filter | Bounded matching records and continuation when needed |
| `aclose` | None; internal only | Release query resources |

Shared guarantees: authorized scope, current-versus-historical context,
`PASS`/`VIOLATION`/`UNKNOWN`, recent-fault visibility, and explicit errors.
Delayed replay must not appear current. Enforce configurable deadlines and
record/byte limits; report truncation. Finalize concrete models and policy values
with the first adapter. Optional provider metadata may extend these results
without changing their meaning.

## Provider selection and comparison

Select `backend` in operator-owned MCP settings. **VictoriaLogs is the default
and only provider planned for the first implementation.** Other selections remain
unimplemented and fail clearly. Each future adapter needs compatible ingestion
and shared behavior tests.

| Provider | Plan | Best fit | Tradeoff |
|---|---|---|---|
| VictoriaLogs | Initial/default | Field/time/text log search | ROS checks and adapter still required |
| ClickHouse | Future | Telemetry joins, SQL analytics | More schema/index design |
| Loki | Future | Existing Grafana deployment | Body searches scan selected chunks |
| SQLite FTS5 | Future | Local/offline investigation | Single writer; shared service needs integration |

These are fit judgments, not benchmark rankings.

## First implementation and validation

Start with one setup/motion diagnostic, buffered export, the VictoriaLogs
adapter, and bounded MCP tools. Replay multi-machine data with brief faults,
recovery, missing/stale feedback, duplicates, and restart/backlog. Verify results,
scope and response limits. Target p95 freshness ≤5 seconds; measure peak delay,
resource use, and response size before choosing retention/capacity.
