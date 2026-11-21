# Hardware catalog — one file per device

Follow the [repository rules](../../AGENTS.md) and [documentation rules](../AGENTS.md).
This directory is the hardware integration and validation resource. Each device
or board has exactly one authoritative `<hardware-id>.json` file alongside this
guide and the shared [schema](catalog.schema.json). Browse the directory or query
MCP; there are no separate model, setup, run, assessment, evidence or page files.

## What belongs in the device file

- Identity: manufacturer, exact model, revisions, aliases, identifiers and kind.
- Specifications: capabilities, limits, units, conditions and manufacturer sources.
- Integration: driver/factory references, presets, supported features and operating
  or firmware-guide links. `integrated` means the software path exists; physical
  validation is a separate fact. Candidate/unsupported devices remain explicit.
- `validation_history`: complete dated tests, each with its setup, firmware/source
  hashes, environment, procedure/specification version, acceptance criteria,
  observations, result, limitations and evidence. Keep failed/incomplete runs.
- Tester and reviewer: `executed_by`, `started_at`, `completed_at`, `validated_by`,
  `validated_at`, organization and review link on each test. Keep execution and
  evidence review distinct. Use UTC ISO 8601 timestamps ending in `Z`, plus the
  original date/timezone where known. Unknown historical values stay `null` with
  reasons; never infer a validator or test time from Git authorship/import dates.
- Small evidence narratives inline, with checksums. Original source paths and
  revisions on migrated evidence are provenance labels, not extra required files. Large recordings or raw logs
  may use durable external links and checksums. No extra metadata file is needed.
- Optional `support_history`: attributed reviews of exact tested capabilities,
  referring to test IDs in the same file. Historical imported reviews remain
  unreviewed when actual attribution is absent.

A test embeds the exact setup and criteria it used. Repeating a small setup across
runs is intentional: the device file remains sufficient to interpret its history.
When several devices share a rig, reference their catalog IDs in setup subjects.
Do not create a second configuration system; protobuf presets and existing
factories remain authoritative. Link board flashing guides rather than copying
operating procedures into the catalog.

## Add or expand hardware

1. Search existing files/aliases and choose a stable device ID. Distinguish hardware
   revisions where compatibility differs; a generic size class is not a model.
2. Add one file using the shared template below or the scaffold command. No index,
   generated page, template file or separate test record needs updating.
3. Link the existing driver and preset, or mark integration candidate/unsupported.
   Describe advertised specifications separately from observed measurements.
4. Before a deliberate hardware test, freeze source, firmware, configuration,
   wiring, power, calibration, workload, clocks and acceptance criteria. Root
   hardware authorization still applies; catalog commands never access devices.
5. Append the complete test to this file. Record actual tester/time, observations,
   checks, evidence and limits. Use ROS interfaces, diagnostics and rosbag2 for
   runtime evidence; use instruments for physical timing/electrical claims.
6. Submit for owner review. Record reviewer/time/link only when review occurs.
   A successful protocol check does not establish physical stopping or endurance.
7. Include the catalog addition or update in the same PR as a device integration.
   Keep its implementation status and source references accurate. The public
   website imports these files, including new device IDs; do not hand-edit UI
   counts or duplicate validation facts in a website record. Device-software
   additions also need a source mapping in the website's curated discovery list.

The public catalog UI belongs to the
[website repository](https://github.com/Joshua-AI-Robotics/Joshua-AI-Robotics.github.io),
whose `AGENTS.md` owns generation and refresh. A device file remains the only
authoritative hardware record. Candidate discovery and software eligibility are
website concerns; their statuses must not imply unrecorded physical validation.

```sh
bazel run //tools:hardware_catalog_cli -- --new --kind board \
  --id vendor-model --output /tmp/vendor-model.json
bazel run //tools:hardware_catalog_cli -- --check
bazel run //tools:hardware_catalog_cli -- --query devices
bazel run //tools:hardware_catalog_cli -- --query history --hardware-id ti-lp-am243
bazel test //tools:hardware_catalog_test //tools/mcp:hardware_catalog_test
```

Run these inside the supported Docker development shell and use root CI flags
when reproducing the test matrix. Scaffolding refuses to overwrite a file and
never guesses a tester or timestamp. Complete all `REPLACE` values before adding
it here. CI checks source/preset references against the actual checkout; adding a device
file needs no Bazel reference list or index update.

## Shared device template

The scaffold fills the category-specific fields from the one common schema.
Start with this structure; inspect the nearest existing device to append a test.

```json
{
  "schema_version": 2,
  "id": "vendor-model",
  "kind": "board",
  "manufacturer": "Vendor",
  "model": "Exact model",
  "aliases": [],
  "revisions": [],
  "identifiers": {},
  "capabilities": ["uart"],
  "specifications": [],
  "integration": {
    "status": "candidate",
    "driver": null,
    "implementations": [],
    "presets": [],
    "guide": "firmware/AGENTS.md",
    "support_scope": "No integration or physical validation established."
  },
  "documentation": [],
  "maintainers": [],
  "category_details": {"mcu": null, "transports": [], "firmware_guides": []},
  "validation_history": [],
  "support_history": [],
  "limitations": ["Not yet integrated or tested."]
}
```

Supported kinds include boards, cameras, actuators, motor drivers, sensors,
transport adapters, hosts, power devices, assemblies and other devices. The schema
requires relevant category details without separate category templates. Test
entries retain the full setup/specification and per-check expected/observed
results; the schema and existing records define those fields.

## History and queries

New tests append to `validation_history`. Keep old tests and reviews unchanged;
append a correction with `supersedes` rather than rewriting history. A changed
criterion gets a new embedded specification version. CI permits integration-note
updates and appended tests while rejecting historical deletion/rewrites or model
identity reassignment. Run `--check-history <full-base-sha>` to reproduce it.

`query_hardware_catalog()` lists bounded device summaries. Use `view="history"`
for complete tests, optionally filtering by hardware ID, kind, result or validator
username. Continue with `next_cursor` and the same filters. Records too large for
one response produce an explicit error; the full device file remains available.
Neither queries nor archived procedures perform a live check or authorize motion.

The original two model/test MCP calls retain their v1 camera compatibility view,
derived directly from these device files. The optional `compatibility` block on
the original camera preserves its projection ordering; new devices need no such
block. Old guide URLs route here. There are no generated JSON metadata copies or
per-device generated pages to maintain.

## Migration and verification

All five devices and thirteen historical tests were consolidated from the former
split layout. Setup, criteria, measured values, small original narratives and
missing-attribution explanations are preserved inline. Four failures remain
visible. No device was tested or evidence review attributed during migration.

Validate schema, references, evidence hashes, timestamps, attribution and result
consistency. Check append-only history, MCP compatibility/pagination and the
canonical Docker tests. Documentation and catalog tests require no hardware.
