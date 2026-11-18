# PJRC Teensy 4.1

Generated from canonical catalog records. Historical results apply only to the listed setup and specification.

- Hardware ID: `pjrc-teensy-41`
- Kind: board
- Integration: intended
- Scope: JoshuaWire serial integration; stationary USB fault checks are recorded. Mixed-scalar physical qualification and physical motion/timing remain open.
- Capabilities: usb-serial, step-dir, scalar-acquisition
- Revisions: Unknown / not recorded

## Model and setup references

- [Structured model](../models/pjrc-teensy-41.json)
- [Operating guide](../../../firmware/teensy/41/AGENTS.md)

## Validation history

| Run / specification | Result | Date / actual time | Executed by | Validated by / time |
| --- | --- | --- | --- | --- |
| [teensy41-partial-write-2026-10-25](../runs/teensy41-partial-write-2026-10-25.json) / teensy41-usb-backpressure-v1 v1 | passed | 2026-10-25 / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |
| [teensy41-tx-fault-2026-10-25](../runs/teensy41-tx-fault-2026-10-25.json) / teensy41-usb-backpressure-v1 v1 | passed | 2026-10-25 / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |

## teensy41-partial-write-2026-10-25

- [Setup](../setups/teensy41-partial-write-setup.json)
- [Specification](../specifications/teensy41-usb-backpressure-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: Unknown
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| reply-integrity | true | passed |
| fault-recovery | true | passed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/teensy41_partial_write_2026-10-25.md); retained; SHA256 `0eb13fa9cde2c39c2d6ccc08f856c29499c7b8c47cdf8ab829aba1b5f6bae037`

Limitations:

- Motor power was disconnected; USB protocol results do not establish physical stopping or GPIO disable timing.
- The 16-byte TX-capacity corner case is a native test; the physical capacity was not measured.

Provenance gaps:

- started_at: Not recorded in the original evidence; migration is not execution or validation.
- completed_at: Not recorded in the original evidence; migration is not execution or validation.
- executed_by: Not recorded in the original evidence; migration is not execution or validation.
- validated_at: Not recorded in the original evidence; migration is not execution or validation.
- validated_by: Not recorded in the original evidence; migration is not execution or validation.
- organization: Not recorded in the original evidence; migration is not execution or validation.
- review_reference: Not recorded in the original evidence; migration is not execution or validation.
- date_timezone: Original bench date does not specify a timezone.
- software.joshua_commit: Exact tested Joshua commit was not recorded; documentation history is not a tested commit.
- software.container_digest: Exact Docker image digest was not retained.

## teensy41-tx-fault-2026-10-25

- [Setup](../setups/teensy41-tx-fault-setup.json)
- [Specification](../specifications/teensy41-usb-backpressure-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: Unknown
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| reply-integrity | true | passed |
| fault-recovery | true | passed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/teensy41_tx_fault_2026-10-25.md); retained; SHA256 `da71f00573e41f1150b5dc43eb105a0551c2346d878aefc331529632e5af6155`

Limitations:

- Motor power was disconnected; USB protocol results do not establish physical stopping or GPIO disable timing.
- The 16-byte TX-capacity corner case is a native test; the physical capacity was not measured.

Provenance gaps:

- started_at: Not recorded in the original evidence; migration is not execution or validation.
- completed_at: Not recorded in the original evidence; migration is not execution or validation.
- executed_by: Not recorded in the original evidence; migration is not execution or validation.
- validated_at: Not recorded in the original evidence; migration is not execution or validation.
- validated_by: Not recorded in the original evidence; migration is not execution or validation.
- organization: Not recorded in the original evidence; migration is not execution or validation.
- review_reference: Not recorded in the original evidence; migration is not execution or validation.
- date_timezone: Original bench date does not specify a timezone.
- software.joshua_commit: Exact tested Joshua commit was not recorded; documentation history is not a tested commit.
- software.container_digest: Exact Docker image digest was not retained.

## Support assessments

- [pjrc-teensy-41-historical-assessment](../assessments/pjrc-teensy-41-historical-assessment.json): **historical_unreviewed**. Imported bench narratives only. No new execution, reviewer attribution or broader support qualification. Reviewer: Unknown / not recorded; time: not recorded.

## Model limitations

- Physical results apply only to the listed unit, firmware, setup and workload; untested capabilities are not qualified.
- Hardware revision and responsible maintainer are not recorded in the imported evidence.
