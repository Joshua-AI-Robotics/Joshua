# Observability backend contract

**Status:** Proposed v1 contract accompanying the
[ROS 2 observability design](ROS2_OBSERVABILITY_DESIGN.md); no classes below are
implemented by this PR. Python notation describes future frozen typed models;
MCP exposes their JSON equivalents, not arbitrary Python objects.

## Boundary and provider selection

The abstraction represents read-only observability queries, independently of
MCP transport and storage syntax. The operator supplies `ObservabilitySettings`:
`backend: str = "victorialogs"`, endpoint, tenant, credential/TLS references, and
server-owned query policy. These are service deployment settings; robot limits
remain in protobuf. Tool arguments cannot replace these settings.

A factory registry maps `victorialogs` to `VictoriaLogsObservabilityBackend` and
injects it into `ObservabilityMcpServer`. Only that adapter ships initially.
Unknown selections fail startup with `INVALID_ARGUMENT`; recognized future
providers without an implementation fail with `UNIMPLEMENTED`. Never silently
switch stores. Collector destination/schema configuration must match the chosen
provider; changing this setting does not migrate data or reconfigure collectors.

## ABC

```python
from abc import ABC, abstractmethod

class ObservabilityBackend(ABC):
    @abstractmethod
    async def health_summary(self, query: HealthQuery,
                             context: QueryContext) -> HealthSummary: ...

    @abstractmethod
    async def inspect_issue(self, query: IssueQuery,
                            context: QueryContext) -> IssueDetails: ...

    @abstractmethod
    async def search_logs(self, query: LogQuery,
                          context: QueryContext) -> LogPage: ...

    @abstractmethod
    async def aclose(self) -> None: ...
```

Methods honor cancellation and the remaining deadline, returning normalized
models or raising `ObservabilityError`. `aclose` is idempotent, releases only
query-client resources, and is not an MCP tool. No method starts/stops Joshua,
opens hardware, writes configuration, ingests data, or controls collectors.

## Shared inputs

| Type | Fields and semantics |
|---|---|
| `Scope` | Optional sets of `machine_ids`, `robot_ids`, `run_ids`, `device_ids`; OR within a set, AND across populated sets. Empty scope means the caller's authorized fleet. |
| `TimeRange` | `start`, `end`: RFC3339 UTC strings, half-open `[start,end)`; `basis`: `received` (default) or `observed`. Observed time requires UTC-normalized source time; unmappable records do not match that search. |
| `TextFilter` | `mode`: `substring` or `regex`; `pattern`: at most 256 UTF-8 bytes; `case_sensitive: bool = True`. Matches normalized `message` only. Substring is literal; regex is unanchored unless explicitly anchored. Case-insensitive matching uses RE2 rules, including for escaped literal patterns. Never accept backend query syntax. |
| `QueryContext` | Trusted `principal: str`, `authorized_scope: Scope`, `request_time: UTC`, `deadline: float` (monotonic), `max_window_s: int`, `max_items: int`, `max_bytes: int`. Server-constructed, never accepted from MCP arguments. |

MCP validates shapes/ranges and rejects requests outside authorization; the
provider applies the same authorized scope to every query. IDs are opaque exact
strings. Time windows must be finite and within deployment policy. Caller
filters may narrow scope but cannot change limits, calibration, or freshness
thresholds. Missing numerical values remain absent/null, never zero.

Initial policy: default lookback 60 seconds, maximum window 24 hours, query
deadline two seconds. Reject future/empty windows. Limit each ID set to 16 IDs,
IDs/node names to 128 UTF-8 bytes, and tags to eight exact key/value pairs
(64/256 bytes). Omitted filters mean unrestricted within authorization; explicitly
empty sets are invalid. Providers bind/escape values instead of splicing query text.

## Requests and results

Every result carries `meta: ResultMeta`. Responses use typed fields below;
provider-native rows and query syntax never escape into the public schema.

| Method | Request fields | Result fields |
|---|---|---|
| `health_summary` | `HealthQuery(scope, fault_window: TimeRange)` | `HealthSummary(meta, machine_counts, check_counts, recent_fault_count, top_issues)` |
| `inspect_issue` | `IssueQuery(machine_id, check_id, run_id, config_revision, window: TimeRange, device_id?, cursor?)` | `IssueDetails(meta, evidence: CheckEvidence?, recent_fault_count, events: list[EvidenceEvent])` |
| `search_logs` | `LogQuery(scope, window: TimeRange, severities?, node?, tags?, text: TextFilter?, cursor?)` | `LogPage(meta, records: list[LogRecord])` |

All fields without `?` are required; IDs are strings. MCP resolves omitted scope,
time, or issue run/config selectors to explicit authorized defaults before calling
the ABC. Severity is `DEBUG|INFO|WARN|ERROR|FATAL|UNKNOWN`; a set is an inclusion
filter. Node/tag predicates are exact and ANDed. Health accepts active inventory
only: nonempty `scope.run_ids` or an observed-time fault window is invalid.
Health evaluates latest status at trusted `request_time`, independently of fault
lookback. Inspection selects evidence at/before `window.end` in the explicit
run/config, independently of `window.start`; its events/counts use the window.
Historical evidence is not a current-health verdict.

| Output type | Required content |
|---|---|
| `ResultMeta` | `schema_version: Literal[1]`, `backend: str`, `generated_at: UTC`, `evaluated_at: UTC|null`, `scope: Scope`, `window: TimeRange`, `freshness: Freshness|null`, `coverage: Coverage|null`, `truncated: bool`, `next_cursor: str|null`, `reasons: list[ReasonCode]` |
| `machine_counts` | `{expected: int, reporting: int, missing: int}` within effective requested scope |
| `check_counts` | `{expected: int, pass: int, violation: int, unknown: int}`; missing expected checks count as unknown |
| `IssueSummary` (`top_issues[]`) | `identity: CheckIdentity`, `verdict: Verdict`, `reasons: list[ReasonCode]`, `last_observed_at: UTC|null`, `recent_fault_count: int`, `recovered: bool` |
| `CheckEvidence` | `identity: CheckIdentity`, `verdict: Verdict`, `reasons: list[ReasonCode]`, `measurements: list[Measurement]`, `limits: list[LimitEvidence]`, `provenance: Provenance`, `freshness: Freshness`, `coverage: Coverage` |
| `EvidenceEvent` | Discriminated union: `FaultEvent` or `LogRecord`, selected by `kind` |
| `FaultEvent` | `kind: Literal["fault"]`, `event_id: str`, `transition: raised|recovered`, `evidence: CheckEvidence` |
| `LogRecord` | `kind: Literal["log"]`, `event_id: str`, `identity: RecordIdentity`, `node: str|null`, `logger: str|null`, `severity: Severity`, `message: str`, `message_truncated: bool`, `tags: dict[str,str]`, `provenance: Provenance` |

`HealthSummary.top_issues` is `list[IssueSummary]`; fault counts are nonnegative
integers. Other request/result types use the field types named above. Required
nullable fields appear as JSON null; optional request fields may be omitted.

| Shared wire type | Exact fields / allowed values |
|---|---|
| `UTC` | RFC3339 UTC string ending in `Z` |
| `Verdict` | `PASS|VIOLATION|UNKNOWN` |
| `ReasonCode` | `MISSING|STALE|IDENTITY_MISMATCH|CONFIG_MISMATCH|CALIBRATION_UNKNOWN|UNIT_UNKNOWN|CLOCK_UNKNOWN|SOURCE_ORDER_UNKNOWN|TRACKING_ERROR|LIMIT_EXCEEDED|DEVICE_FAULT|INCOMPLETE_DATA` |
| `RecordIdentity` | `machine_id: str`, `robot_id: str|null`, `run_id: str`, `device_id: str|null`, `config_revision: str` |
| `CheckIdentity` | All `RecordIdentity` fields plus `check_id: str` |
| `Freshness` | `status: FRESH|STALE|UNKNOWN`, `oldest_required_age_ms: int|null` |
| `Coverage` | `expected_checks: int`, `reported_checks: int`, `unknown_checks: int` |
| `Measurement` | `name: str`, `value: float|null`, `unit: str|null`, `source_id: str`, `observed_at: UTC|null` |
| `LimitEvidence` | `config_revision: str`, `config_path: str`, `unit: str`, `lower: float|null`, `upper: float|null`, `tolerance: float|null` |
| `Provenance` | `producer_id: str`, `producer_epoch: int`, `boot_id: str`, `sequence: int`, `source_kind: ros_log|process_log|diagnostic|measurement`, `source_topic: str|null`, `source_time_ns: str|null`, `clock_domain: str`, `observed_at: UTC|null`, `received_at: UTC` |

Freshness ages are recomputed at `meta.evaluated_at`: request time for live
health, window end for historical inspection. Log pages set evaluated_at,
freshness and coverage to null. Unmappable source times leave observed_at null.
Units must agree for comparison; missing values/units cause unknown checks.
Limits require at least one bound/tolerance. Non-null numbers must be finite;
counts/epochs/sequences/ages are nonnegative. Reason lists are bounded to 12;
provenance topics are bounded to 256 bytes. Log messages can be clipped only
with `message_truncated=true`.

Common event identity is `(machine_id, producer_id, boot_id, sequence)`; retries
retain it, distinct repeated faults receive distinct identities. Results reference the
same canonical normalized records exported by collectors, with versioned schema
mapping supplied by each provider. Ingestion itself is outside this query ABC.

Health requires expected inventory and active run/config plus producer assignment.
Assignment records identify each check's active producer ID, monotonic producer
epoch and boot ID; publisher/exporter startup registers that assignment through
its trusted deployment channel, without a second live-state service. Within the
active assignment, greatest sequence wins, never receipt order. Older epochs and
delayed replays remain historical. Conflicting/missing assignments yield
`UNKNOWN` with `SOURCE_ORDER_UNKNOWN`; measurement age still determines freshness.
Missing/stale/mismatched or uncalibrated evidence yields `UNKNOWN`; lack of logs
never implies `PASS`. Query failure is an error, not an empty healthy fleet.
If expected inventory is unavailable, return `FAILED_PRECONDITION` rather than
invent coverage. Preserve recent fault counts even after the current verdict
recovers. Deduplicate canonical `raised` fault events, not message text, recovery
events or periodic snapshots. Current counts and recent-fault counts remain distinct.

## Bounds, paging, and errors

| Result | Hard initial serialized JSON budget, including metadata |
|---|---|
| `HealthSummary` | 2 KiB; at most five top issues; counts cover the effective requested scope within authorization |
| `IssueDetails` | 4 KiB; at most 20 evidence events |
| `LogPage` | 4 KiB; at most 20 log records |

Enforce budgets in the MCP adapter after provider normalization, with provider
query budgets preventing unbounded retrieval. Preserve required metadata/counts;
truncate lists/strings with explicit truncation indicators. If the required
envelope cannot fit, fail with `RESOURCE_EXHAUSTED`. Never silently truncate
measurements, units, verdicts, or identity. Byte limits are not token guarantees.

Issue/log cursors are opaque, expiring, and bound to provider, principal, scope,
filters, and a frozen time window; changing any invalidates them. Order records
deterministically by selected time then event identity. Pagination is not a
cross-query storage snapshot; late data can appear in a subsequent fresh query.
Health uses aggregate counts plus top issues, without paging the fleet into the LLM.

`ObservabilityError` contains `code`, bounded safe `message`, and `retryable`.
Use existing canonical codes: `INVALID_ARGUMENT`, `UNAUTHENTICATED`,
`PERMISSION_DENIED`, `DEADLINE_EXCEEDED`, `UNAVAILABLE`, `UNIMPLEMENTED`,
`FAILED_PRECONDITION`, `RESOURCE_EXHAUSTED`, `DATA_LOSS`, `CANCELLED`, `INTERNAL`.
Unsupported filters return `UNIMPLEMENTED`, never weaker matching. Hide credentials,
raw backend errors, and query internals. Data gaps are successful results with
unknown coverage/verdicts; backend/authorization failures are errors.

## Initial implementation and conformance

Implement the ABC and frozen models, one factory/registry entry, a VictoriaLogs
adapter translating to LogsQL/HTTP, and an MCP wrapper with these schemas/bounds.
The vendor MCP toolset does not supply this contract automatically. No extra
live-state service, ingestion ABC, or second provider is required initially.

Shared contract fixtures must verify identical scope/time/text semantics,
inventory/UNKNOWN behavior, current and recovered faults, duplicate handling,
stable identities, optional measurements, deadlines/cancellation, paging,
authorization, serialized budgets, and cleanup. Future providers must satisfy
these fixtures and supply a compatible ingestion/schema mapping before selection.
