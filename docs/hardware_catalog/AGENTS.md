# Hardware catalog and validation evidence

Follow [root instructions](../../AGENTS.md) and [documentation rules](../AGENTS.md).
This directory owns the catalog's data contract, contribution workflow and reusable
record templates. Start with the generated [catalog index](INDEX.md).

## Ownership and scope

Models own identity, advertised specifications and intended integration. Setups
own the exact physical subjects, topology, wiring, power, firmware, configuration,
calibration and environment. Versioned specifications own procedures, measurement
methods and acceptance criteria. Runs own observations and evidence. Assessments
own reviewed support claims. Use stable IDs and references between these records.

The [schema](schemas/catalog.schema.json) defines their fields. The
[shared catalog tool](../../tools/hardware_catalog.py) owns loading, validation,
queries, scaffolding and generation; MCP delegates to it. Protobuf presets remain
the runtime configuration authority. Catalog snapshots record what was actually
used and never become another configuration parser or hardware lifecycle owner.

Intended integration, a historical pass, a reviewed assessment, independent
reproduction and currently connected hardware readiness are different facts.
Assess capabilities under specific setups; never assign a universal verified
badge to a model from a handshake or a software-only test. Retain failures and
inconclusive results alongside successes. No catalog operation opens hardware,
flashes firmware, launches a robot or performs a live check.

## Record templates

Templates are drafts, excluded from the catalog. Replace every `REPLACE` value,
fill references and remove unsupported claims before submission. Do not invent
values to satisfy a field. Unknown historical values use `null` with a reason in
`unknowns`; model/setup limitations identify incomplete identity or specifications.

| Template | Required category context |
| --- | --- |
| [Board](templates/board.json) | MCU, transports, firmware/boot guide and electrical constraints |
| [Camera](templates/camera.json) | Capture modes, exposure, calibration and timestamp semantics |
| [Actuator](templates/actuator.json) | Units, gearing/feedback, load, calibration and physical limits |
| [Motor driver](templates/motor_driver.json) | Modes, electrical limits, faults and command/stop behavior |
| [Sensor](templates/sensor.json) | Quantity, units, range, sample rate and reference/calibration |
| [Transport adapter](templates/transport_adapter.json) | Interface/chipset, driver, topology, bitrate and timing/loss |
| [Host](templates/host.json) | Architecture, OS/kernel, compute resources and controllers/NICs |
| [Power](templates/power.json) | Supply, voltage/current limits, protection and measuring instruments |
| [Assembly](templates/assembly.json) | Referenced parts, wiring/topology and combined workload |
| [Other](templates/other.json) | Explain the device and its capability-specific validation requirements |
| [Setup](templates/setup.json) | Physical unit aliases, revisions, wiring, power, firmware, software environment, configuration and calibration |
| [Specification](templates/specification.json) | Version, prerequisites, workload, steps, clocks, measurement point, checks and evidence requirements |
| [Run](templates/run.json) | Setup/specification, actual software and observations, execution/review provenance, evidence and limitations |
| [Assessment](templates/assessment.json) | Exact supported scope, linked runs, reviewer/time and unresolved limitations |

All model kinds share one schema with category-specific required fields. Devices
can expose several capabilities. A board is not a motor; catalog the board,
drive and sensor separately when their identities are known and reference them
as setup subjects. Do not turn a generic size class or unverified alias into an
exact model. Use public unit aliases rather than private serial numbers.

Specifications distinguish manufacturer claims, configured limits and measured
values. Each specification value records its unit, conditions and source. Runs
record measurement outcomes separately. Every specification check has an explicit
observed value or a not-run/inconclusive outcome; no missing check becomes a pass.
An overall result follows the checks. A passed USB/protocol result does not prove
physical GPIO timing, actuator stopping, electrical safety or endurance.

## Who and when

Each run records `date`, its original `date_timezone`, `started_at`, `completed_at`,
`executed_by`, `validated_at`, `validated_by`, `organization`, `review_reference`
and `review_status`. Actual timestamps use ISO 8601 UTC ending in `Z`. Contributor
identities use a provider, public username and optional organization. Execution
and evidence review are separate events, even if performed by the same person.

A reviewed result needs the reviewer, validation timestamp and HTTPS review
reference together. Independent reproduction uses `reproduces` to reference the original run,
with its own setup,
operator and evidence. Do not infer an operator or validation date from a Git
author, import date, assistant session or document creation. Imported records
remain historical/unreviewed unless evidence establishes an actual review.
The generated pages expose these gaps rather than silently claiming validation.

## Add a device or validation run

1. Search [existing models](INDEX.md), aliases and revisions; reuse stable IDs.
   Register a new model as catalogued/unsupported if no integration exists.
2. Scaffold the appropriate model and setup. Link existing factories/drivers,
   protobuf presets and board flashing guides. Record advertised limits with
   provenance; use per-category fields instead of a second discovery registry.
3. Select an existing versioned specification or propose a reviewed new version.
   Define acceptance criteria before a new run. Historical reconstruction is
   labeled explicitly and does not retroactively claim predeclared thresholds.
4. Freeze the setup, source commit plus any retained patch, build flags, container
   image, firmware hash, dependencies, configuration and calibration. Record exact
   wiring, power, load, topology, instruments and relevant ambient conditions.
5. Follow root hardware authorization for a deliberate test. Use ROS topics,
   diagnostics, logging and rosbag2 for runtime evidence. Record QoS and clock
   domains. Use instruments for facts ROS cannot measure, such as electrical levels
   or physical stopping. Do not add a parallel telemetry or monitoring runtime.
6. Add the run, commands, logs, measurements, per-check outcomes and limitations.
   Publish failures and incomplete runs too. Keep artifacts in durable storage:
   retained local files and external evidence require SHA256; a deleted CI
   artifact is not durable evidence. Remove private imagery, credentials and
   private unit/location information before sharing; record any resulting gap.
7. Submit a PR for owner review. A new run does not automatically broaden support.
   A support assessment names exact capabilities/conditions and references passing
   evidence. Set reviewer/time/reference only when that review actually occurs.

From the repository root, inside the supported Docker development shell:

```sh
bazel run //tools:hardware_catalog_cli -- --new model --kind board \
  --id vendor-board-model --output /tmp/vendor-board-model.json
bazel run //tools:hardware_catalog_cli -- --new run \
  --id vendor-board-run --output /tmp/vendor-board-run.json
bazel run //tools:hardware_catalog_cli -- --write-docs
bazel run //tools:hardware_catalog_cli -- --check
bazel run //tools:hardware_catalog_cli -- --query runs --hardware-id ti-lp-am243
bazel test //tools:hardware_catalog_test //tools/mcp:hardware_catalog_test
```

These commands do not execute a test procedure or device command. Supply the
root's stack/Python flags when reproducing CI. `--new` writes only a new explicit
output file and refuses to overwrite it. Copy completed records into the matching
collection with filename `<id>.json`. Declare new local source/guide references in
[BUILD](BUILD) so isolated Bazel integrity tests can resolve them.

## History, releases and review

Never reuse an ID or silently rewrite an executed run/specification's historical
meaning. Add a new run/assessment with `supersedes` for corrections; retain the
original and explain the correction in the PR. A changed specification gets a new
ID/version. CI checks references and generated output, not real hardware behavior.

A newer failure must remain visible. Relevant changes to firmware, driver, board
revision, wiring, OS/kernel, middleware or workload require reassessment of the
applicable scope; old evidence remains valid as history. Compare exact conditions
rather than assuming a newer Joshua release inherits an old pass. CI also rejects changes/removal of historical setups, specifications, runs,
assessments and evidence relative to the PR base; add new versions or superseding
records. Historical model identity and IDs are preserved too; integration notes
and aliases can evolve without reassigning a model ID. Use `--check-history <full-base-sha>` to reproduce that check. Schema changes
need an explicit migration and compatibility tests. Catalog ownership follows
CODEOWNERS; the root contribution entrypoint links to this guide.

## Migration and compatibility

The existing two camera records, stationary Teensy tests, AM243 boot/SOES checks,
failed SOES endurance attempts, initial RNG startup failure, delayed-dispatch
check, vendor LRW failure and undated vendor bring-up
are retained. ESP32 and Arduino have no physical validation imported. The original
bench narratives are retained under `evidence/`; operational guides link here.
No new hardware test or evidence review was performed during migration.

[hardware_catalog.json](../hardware_catalog.json) and
[hardware_test_records.json](../hardware_test_records.json) are generated v1
compatibility projections for the original model/test API, not editable sources.
Their original model/run IDs, measurements, environment and limitations are
preserved. New categories and incomplete historical records are available through
the full catalog and `query_hardware_catalog`, without fabricating v1 provenance.
[SUPPORTED_HARDWARE.md](../SUPPORTED_HARDWARE.md) and
[TESTED_HARDWARE.md](../TESTED_HARDWARE.md) remain stable generated entrypoints.

Queries accept entity, hardware ID, text, run result or validator username plus a
bounded limit/cursor. Invalid entity/filter combinations and stale cursors fail
explicitly. No page silently truncates results; `next_cursor` indicates more data.

## Verification

Run schema/reference/checksum, negative-fixture, generated-output and MCP tests.
Check the applicable instructions and local links, then the canonical Docker
matrix. New hardware never needs to be connected for catalog integrity checks.
