# Device software catalog — one file per integration

Follow the [repository rules](../../AGENTS.md) and
[documentation rules](../AGENTS.md). This directory owns the discovery and
integration records for software that captures sensors, controls devices, or
implements device communication. Each driver, SDK or library has one
`<software-id>.json` record and uses the shared [schema](catalog.schema.json).
The [hardware catalog](../hardware_catalog/AGENTS.md) owns physical device records.

## Contribution contract

Keep the exact upstream project, primary HTTPS source, aliases, category,
description, and dated discovery snapshot in the record. The discovery date is
research provenance, not a physical test date or an integration validation date.
Use `candidate`, `integrated`, `unsupported`, or `retired` explicitly.

An integrated record needs its actual implementation path in Joshua. Do not infer
integration from dependency names, upstream ROS support, or manufacturer claims.
The validator rejects duplicate integrated implementation paths. Pybricks is a
bench-control tool and SOES is an opt-in firmware path; retain those limitations
in their descriptions. Simulators, learning frameworks, planners, and general
debugging tools are outside this catalog and the software denominator.

Update this record in the same PR as an integration, retirement, or source-path
change. One file is sufficient; do not maintain a website copy, index, manual
percentage, or second source mapping. Software implementation does not establish
physical qualification. Record actual hardware test evidence in its device file.

## Export and publication

The shared [catalog tool](../../tools/hardware_catalog.py) validates both catalogs
and exports their searchable summaries from a clean committed checkout:

```sh
bazel run //tools:hardware_catalog_cli -- --check
bazel run //tools:hardware_catalog_cli -- --export --output /tmp/catalog.json
bazel test //tools:hardware_catalog_test
```

Use the supported Docker stack and root CI flags. The public website reads this
export and publishes it daily. It owns presentation and scheduling, while Joshua
owns every source record, counting rule, schema and export function. Generated
data is a deployment artifact rather than a committed website source file.

## Verification

Check schema, identity, category, source URLs and integrated implementation
references. Exercise a valid integration, a candidate, a missing source, duplicate
implementation counting, and dirty-checkout rejection. No catalog operation may
open a device or launch the robot runtime.
