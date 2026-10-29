# ROS 2 observability through VictoriaLogs and MCP

**Status:** Proposed; no runtime implementation or hardware verification.

## Goal and decision

Debug multiple machines: is the expected setup present, is measured motion
following commands, and are observed values within configured limits? Target
five-second observation-to-MCP freshness; roughly one second is desirable.
Minimize LLM tokens through deterministic checks and bounded summaries.

Use central VictoriaLogs for structured diagnostic records and searchable logs.
MCP queries it directly; start without a separate live-state service. This is a
provisional choice pending representative replay measurements.

## Data flow

```mermaid
flowchart LR
    R[Existing ROS feedback and logs] --> D[Per-machine checks and ROS diagnostics]
    D --> V[Vector with bounded disk buffer]
    V --> L[Central VictoriaLogs]
    L --> M[Read-only bounded MCP]
    M --> A[AI client]
```

Reuse [diagnostic_updater and diagnostic_aggregator](https://github.com/ros/diagnostics)
for publishing and grouping diagnostics. Joshua-specific checks compare the
[authoritative protobuf config](../config/README.md) with observed device/runtime
identity, topic connectivity, and calibrated command/feedback values. Subscribe
to existing feedback; preserve device/bus ownership. A leader sensor or accepted
command does not establish follower motion. Respect each
[message's units and feedback capabilities](../ros2/README.md#typed-position-and-actuator-messages).

Evaluate checks at the appropriate feedback rate. Emit periodic status around
once per second even when unchanged, plus brief-fault events/counters so
one-second summaries do not hide observed excursions. Sampled data cannot prove
what occurred between samples. Capture relevant stdout/stderr alongside ROS logs;
assign one owner per source to prevent domain-wide collection duplicates.

[Vector](https://docs.victoriametrics.com/victorialogs/data-ingestion/vector/)
batches JSONL records and sends them to VictoriaLogs. Configure disk buffering,
retry, capacity/overflow policy, and delivery-error reporting. Buffering aids
recovery; it does not preserve query availability during a database outage.

## Evidence and freshness contract

Each record carries machine/robot/run/device identity, node/logger, event kind,
severity, active config revision, producer boot/sequence identity, source
provenance, measurement time/clock domain, and collector receipt time. Diagnostic
records additionally carry check ID, measurements, units, applicable limits or
tolerance references, coverage, and verdict: `PASS`, `VIOLATION`, or `UNKNOWN`.
Limits are versioned evidence derived from config, never an independent policy.

Publish expected inventory from deployment assignments and protobuf config;
queries must identify machines that have never reported. Select coherent latest
records per check and active run/config. Summaries also include deduplicated
fault counts and recent excursions over a bounded lookback; recovery must not
erase recent violations. Unknown units/calibration, mismatched
identity/config, unavailable measurements, clock uncertainty, missing records,
or stale feedback produce `UNKNOWN`. `PASS` covers only the stated observed
checks, not physical readiness. Freshness thresholds are signal-specific,
independent of the five-second display budget; delayed replay must not look live.

VictoriaLogs documents [up to one second of ingestion visibility delay](https://docs.victoriametrics.com/victorialogs/#forced-flush).
Budget collection, buffering, transport, visibility, and query time together.
Use bounded snapshot queries; routine forced flushes are discouraged.

## Low-token MCP interface

Expose two typed operations backed by server-side filters/aggregates:

| Operation | Proposed hard response bounds |
|---|---|
| `health_summary(scope, time_range)` | Counts/coverage, freshness, at most five top issues; 2 KiB |
| `inspect_issue(machine_id, check_id, time_range)` | Measurements, limits, provenance, selected log context; 20 events, 4 KiB |

Enforce time windows, query deadlines, output bytes, and truncation/pagination
in code. Authenticated scope authorizes which machines may be queried. Use
query-only backend access and expose no control operations. The
[vendor MCP server](https://github.com/VictoriaMetrics/mcp-victorialogs)
provides query tools; these typed contracts and hard bounds remain custom work.
Monitoring invokes no LLM. The AI requests summaries and drills down on demand;
the freshness target ends at the MCP result, not completion of AI prose.

## Boundaries and acceptance

Limit enforcement and protective stopping belong to local controllers/drives;
this proposal neither implements nor verifies them. This layer neither starts
hardware nor owns runtime lifecycle. Raw sensor recordings,
automatic repair, a metrics database, and database clustering are outside the
initial increment. Reconsider ClickHouse if numerical telemetry joins dominate;
evaluate ros2_medkit before building broader remote-diagnostic gateway features.

Replay a representative multi-machine recording containing a brief tracking
fault, missing host, stale/misassociated feedback, config change, duplicate
delivery, clock skew, and collector restart/backlog. Verify expected verdicts,
read-only scope and response bounds. Acceptance proposal: p95 end-to-end freshness
at most five seconds under the stated workload, reporting p99, peak delay, ingest
lag, query latency, RAM/disk growth, and response bytes. Attempt one second
without promising it. Before production, choose measured retention/capacity,
buffer overflow behavior, and acceptable downtime/data loss.
