# ROS 2 observability design

**Status:** Proposed architecture; this PR implements documentation only.

## Principles

- Report observed evidence, its source, coverage, and freshness; missing evidence
  is `UNKNOWN`.
- Keep robot configuration authoritative in protobuf. Observability records
  reference the active revision rather than introducing another control policy.
- Keep control, limit enforcement, and protective stopping local to their
  implementing subsystem. Observation does not certify physical readiness.
- Perform continuous checks and aggregation without an LLM. Return small
  summaries first and detailed evidence only on request.
- Separate MCP transport, observability contracts, and storage providers. User
  selection changes the provider, not tool names or response semantics.
- Reuse ROS diagnostics and existing feedback interfaces; preserve device/bus
  ownership and measure performance before promising latency.

## Goal

Answer whether each ROS 2 machine matches its expected setup, measured motion
follows commands, and observed values meet configured limits. Target five-second
observation-to-MCP freshness; roughly one second is desirable. AI prose generation
is outside that budget. Process presence and command acceptance alone do not
establish correct physical motion.

## Collection and diagnostic pipeline

```mermaid
flowchart LR
    R[Existing ROS feedback and logs] --> D[Per-machine checks and ROS diagnostics]
    D --> C[Buffered collector]
    C --> S[Selected storage provider]
    S --> B[ObservabilityBackend ABC implementation]
    B --> M[Backend-neutral MCP tools]
    M --> A[AI client]
```

Reuse [ROS diagnostics](https://github.com/ros/diagnostics) for publication and
aggregation. Joshua-specific checks compare [protobuf expectations](../config/README.md)
with existing feedback using the [documented unit contracts](../ros2/README.md#typed-position-and-actuator-messages).
Preserve device/bus ownership; leader feedback does not verify follower motion.
Evaluate at the appropriate feedback rate, emit status around once per second,
and preserve brief-fault events. Assign one collector per source, including
stdout/stderr provenance, to prevent duplicate counting.

Initially, [Vector sends JSONL to VictoriaLogs](https://docs.victoriametrics.com/victorialogs/data-ingestion/vector/)
with bounded disk buffering, retries, overflow policy, and delivery-error
reporting. Future providers need matching collector destinations/schema mappings.
Buffering aids recovery but does not keep queries available during a central outage.

## Evidence and freshness

Records preserve identity/config revision, producer boot/sequence, provenance,
source time/clock domain, receipt time, measurements/units, and limits/tolerances.
Expected inventory includes never-seen hosts. Summaries combine current status
for the active run/config with deduplicated recent faults, including recovered
excursions. Missing/stale/misassociated or uncalibrated evidence is `UNKNOWN`;
`PASS` covers only the stated observed checks. Freshness is per signal; delayed
replay must not appear live. Sampling cannot prove behavior between samples.

Measure collection through query time together. VictoriaLogs documents
[up to one second of ingestion visibility delay](https://docs.victoriametrics.com/victorialogs/#forced-flush);
routine forced flushes are discouraged.

## Abstract observability interface and MCP

`ObservabilityBackend(ABC)` defines storage-independent typed operations.
`ObservabilityMcpServer` validates requests, supplies trusted authorization/query
budgets, delegates, and bounds serialized output. Providers translate queries
and normalize results. The [contract](OBSERVABILITY_BACKEND_CONTRACT.md) defines
types, errors, settings, and lifecycle.

| ABC operation / MCP tool | Input | Output and hard initial bounds |
|---|---|---|
| `health_summary` | Scope and bounded fault lookback | Inventory/check counts, freshness, recent faults, up to five issues; 2 KiB |
| `inspect_issue` | Machine, check, run/config, optional device, window | Evidence as of window end, limits, recent faults/log context; 20 records, 4 KiB |
| `search_logs` | Time/scope, severity/tags, substring or regex, cursor | Selected normalized log fields; 20 records, 4 KiB |
| `aclose` (internal only) | None | Release query-client resources |

Use health summaries first and drill down on demand. Tool arguments contain no
provider syntax, endpoints, credentials, or backend selector. Monitoring invokes
no LLM. The vendor MCP does not supply this custom contract or its hard limits.

## Provider choice and technology comparison

The operator selects `backend` in MCP deployment settings. Its default is
`victorialogs`; the initial implementation supplies only
`VictoriaLogsObservabilityBackend`. This PR remains documentation-only.
Unimplemented selections fail startup, without silent fallback. Other adapters
must pass the same contract tests before registration.

| Provider | Implementation plan | Best fit | Main tradeoff |
|---|---|---|---|
| [`victorialogs`](https://docs.victoriametrics.com/victorialogs/keyconcepts/) | Default; only initial provider | Shared diagnostic/log search | ROS export, checks, and typed adapter still need implementation |
| [`clickhouse`](https://clickhouse.com/docs/reference/engines/table-engines/mergetree-family/textindexes) | Future; unimplemented | Telemetry joins and SQL analytics | More schema/index/ingestion design |
| [`loki`](https://grafana.com/docs/loki/latest/get-started/overview/) | Future; unimplemented | Existing Grafana deployments | Body searches scan selected chunks |
| [`sqlite_fts5`](https://www.sqlite.org/wal.html) | Future; unimplemented | Local archives/offline debugging | One writer; shared service requires integration |

These are fit judgments, not benchmarks. Prometheus/VictoriaMetrics could add
numeric trends, and ros2_medkit could add ROS discovery/fault access; neither
replaces this complete query contract or is an initial dependency.

## Initial increment and acceptance

Implement one configuration/command-feedback diagnostic, normalized records,
buffered export, VictoriaLogs adapter, and bounded MCP tools. Raw sensor capture,
automatic repair, a separate live-state service, and clustering are outside this
increment. Local enforcement/protective stopping require separate verification.

Replay multi-host data with brief/recovered faults, missing hosts, stale/wrong
feedback, config changes, duplicates, clock skew, and collector restart/backlog.
Verify verdicts, authorization, output bounds, and provider selection/failure.
Proposed acceptance: p95 observation-to-MCP freshness at most five seconds under
a stated workload; report p99/peak delay, ingest/query latency, resource use,
and response bytes. Attempt one second without promising it. Choose retention,
buffer capacity/overflow, and availability/data-loss requirements before production.
