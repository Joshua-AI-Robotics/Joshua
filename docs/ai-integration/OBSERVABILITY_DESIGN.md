# Observability and outcome verification design

**Status:** Proposed design for discussion
**Checkpoints:** 1. Inspection errors; 2–3. Verified tasks; 4. Full hardware coverage

## Goal

Establish what the robot actually did and whether the task met its expected
outcome. Inspection errors are visible in checkpoint 1. Status, faults, and
required measured feedback ship with the first robot operation in checkpoint 2.

## Proposed design

A run records its target, task/configuration revision, parameters, progress,
outcome, and required observations. Feedback includes source, units, timing,
and validity.

Command acceptance, device acknowledgement, and measured task completion are
distinct evidence. Completion checks use correctly associated, fresh feedback
with a defined conversion and tolerance. Missing evidence remains visible.

At the current source baseline, position feedback can use native units and
serial buses have process ownership constraints. Define command-to-sensor
mapping, calibration, and a supported feedback path before claiming verified
angular movement. Feedback from another arm cannot establish target-arm motion.

The backend evaluates outcome checks and the task's failure policy.
Time-sensitive motor control remains with the implementing controller.
Stop/cancel controls must remain usable when the model is unavailable.

Start with structured run events and the observations needed by the selected
operation. Rich dashboards, exporters, and bulk capture follow when justified.

## Suggested PR scopes

1. Minimum operation status, errors, source identity, and units.
2. Supported observation ownership, conversion, timing, and freshness.
3. Task completion checks, cycle accounting, and missing-feedback/fault tests.

Physical qualification names the exact setup and observations covered.
Passing software tests does not establish physical task success.

## Decisions for review

Which sensor verifies the first task? What units and tolerance apply? How does
feedback share resources with commands? What is the defined stop behavior?

Related: [AI integration guide](../AI_INTEGRATION.md),
[reusable tasks](REUSABLE_TASKS_DESIGN.md), and
[configuration and feedback constraints](../../config/README.md).
