# Texas Instruments LP-AM243

Generated from canonical catalog records. Historical results apply only to the listed setup and specification.

- Hardware ID: `ti-lp-am243`
- Kind: board
- Integration: intended
- Scope: JoshuaWire UART and explicit TI/SOES EtherCAT profiles; software-only actuator channel. TI vendor demo is historical; SOES endurance is blocked.
- Capabilities: uart, ethercat, software-actuator-channel
- Revisions: Unknown / not recorded

## Model and setup references

- [Structured model](../models/ti-lp-am243.json)
- [Operating guide](../../../firmware/am243/AGENTS.md)

## Validation history

| Run / specification | Result | Date / actual time | Executed by | Validated by / time |
| --- | --- | --- | --- | --- |
| [am243-boot-identity-2026-10-26](../runs/am243-boot-identity-2026-10-26.json) / am243-boot-identity-v1 v1 | passed | 2026-10-26 / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |
| [am243-delayed-dispatch-2026-10-25](../runs/am243-delayed-dispatch-2026-10-25.json) / am243-delayed-dispatch-v1 v1 | passed | 2026-10-25 / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |
| [am243-initial-rng-startup-failure-2026-10-26](../runs/am243-initial-rng-startup-failure-2026-10-26.json) / am243-rng-startup-v1 v1 | failed | 2026-10-26 / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |
| [am243-soes-endurance-attempt-1-2026-10-26](../runs/am243-soes-endurance-attempt-1-2026-10-26.json) / am243-soes-endurance-v1 v1 | failed | 2026-10-26 / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |
| [am243-soes-endurance-attempt-2-2026-10-26](../runs/am243-soes-endurance-attempt-2-2026-10-26.json) / am243-soes-endurance-v1 v1 | failed | 2026-10-26 / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |
| [am243-soes-protocol-2026-10-26](../runs/am243-soes-protocol-2026-10-26.json) / am243-soes-protocol-v1 v1 | passed | 2026-10-26 / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |
| [am243-vendor-ethercat-undated](../runs/am243-vendor-ethercat-undated.json) / am243-vendor-ethercat-v1 v1 | passed | Unknown date / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |
| [am243-vendor-hello-world-undated](../runs/am243-vendor-hello-world-undated.json) / am243-vendor-hello-world-v1 v1 | passed | Unknown date / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |
| [am243-vendor-lrw-failure-undated](../runs/am243-vendor-lrw-failure-undated.json) / am243-vendor-lrw-v1 v1 | failed | Unknown date / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |

## am243-boot-identity-2026-10-26

- [Setup](../setups/am243-jw-boot-setup.json)
- [Specification](../specifications/am243-boot-identity-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: UTC
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| boot-distinct | true | passed |
| session-stable | true | passed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/am243_boot_identity_2026-10-26.md); retained; SHA256 `f76e9f138d4241760f5b66a429873b588c44afb39e2d9478d12ef98cca1941d1`

Limitations:

- Corrected image boot/session checks only; initial missing SA2UL firewall grant caused a data abort.
- No entropy quality, OP traffic, watchdog latency or physical motor safety was qualified.

Provenance gaps:

- started_at: Not recorded in the original evidence; migration is not execution or validation.
- completed_at: Not recorded in the original evidence; migration is not execution or validation.
- executed_by: Not recorded in the original evidence; migration is not execution or validation.
- validated_at: Not recorded in the original evidence; migration is not execution or validation.
- validated_by: Not recorded in the original evidence; migration is not execution or validation.
- organization: Not recorded in the original evidence; migration is not execution or validation.
- review_reference: Not recorded in the original evidence; migration is not execution or validation.
- software.joshua_commit: Exact tested Joshua commit was not recorded; documentation history is not a tested commit.
- software.container_digest: Exact Docker image digest was not retained.

## am243-delayed-dispatch-2026-10-25

- [Setup](../setups/am243-ti-demo-delay-setup.json)
- [Specification](../specifications/am243-delayed-dispatch-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: Unknown
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| late-dispatch | true | passed |
| stop-image | true | passed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/am243_delayed_dispatch_2026-10-25.md); retained; SHA256 `63a992923cb18ee33d53dbf1d30ad26a6284830346231acdcab41241eeea10d2`

Limitations:

- Uses retired TI demo host path and existing firmware, not current JW runtime qualification.
- No production scheduling bound or physical stopping time was measured.

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

## am243-initial-rng-startup-failure-2026-10-26

- [Setup](../setups/am243-initial-rng-startup-setup.json)
- [Specification](../specifications/am243-rng-startup-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: UTC
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| startup | false | failed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/am243_boot_identity_2026-10-26.md); retained; SHA256 `f76e9f138d4241760f5b66a429873b588c44afb39e2d9478d12ef98cca1941d1`

Limitations:

- The initial image hash and exact tested source commit were not retained.
- Later corrected startup is a separate passing run; this initial failure must not be hidden by that success.

Provenance gaps:

- started_at: Not recorded in the original evidence; migration is not execution or validation.
- completed_at: Not recorded in the original evidence; migration is not execution or validation.
- executed_by: Not recorded in the original evidence; migration is not execution or validation.
- validated_at: Not recorded in the original evidence; migration is not execution or validation.
- validated_by: Not recorded in the original evidence; migration is not execution or validation.
- organization: Not recorded in the original evidence; migration is not execution or validation.
- review_reference: Not recorded in the original evidence; migration is not execution or validation.
- software.joshua_commit: Exact tested Joshua commit was not recorded; documentation history is not a tested commit.
- software.container_digest: Exact Docker image digest was not retained.

## am243-soes-endurance-attempt-1-2026-10-26

- [Setup](../setups/am243-soes-endurance-attempt-1-setup.json)
- [Specification](../specifications/am243-soes-endurance-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: Unknown
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| endurance-complete | false | failed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/am243_soes_2026-10-26.md); retained; SHA256 `28487c96d9d29c852882f9eb280a02af97eb00cf21583726d06410755f4d2a49`

Limitations:

- Endurance did not complete; register timeout remains a qualification blocker.
- No root cause, host NIC fix, physical outputs or timing guarantee established.

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

## am243-soes-endurance-attempt-2-2026-10-26

- [Setup](../setups/am243-soes-endurance-attempt-2-setup.json)
- [Specification](../specifications/am243-soes-endurance-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: Unknown
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| endurance-complete | false | failed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/am243_soes_2026-10-26.md); retained; SHA256 `28487c96d9d29c852882f9eb280a02af97eb00cf21583726d06410755f4d2a49`

Limitations:

- Endurance did not complete; register timeout remains a qualification blocker.
- No root cause, host NIC fix, physical outputs or timing guarantee established.

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

## am243-soes-protocol-2026-10-26

- [Setup](../setups/am243-soes-setup.json)
- [Specification](../specifications/am243-soes-protocol-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: Unknown
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| protocol-checks | true | passed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/am243_soes_2026-10-26.md); retained; SHA256 `28487c96d9d29c852882f9eb280a02af97eb00cf21583726d06410755f4d2a49`

Limitations:

- Software actuator channel only; no physical STEP/DIR GPIO output.
- Protocol checks do not qualify endurance; the separate endurance attempts failed.

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

## am243-vendor-ethercat-undated

- [Setup](../setups/am243-vendor-ethercat-setup.json)
- [Specification](../specifications/am243-vendor-ethercat-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: Unknown
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| vendor-bringup | true | passed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/am243-vendor-ethercat.md); retained; SHA256 `cb3b6ea2e048a15987e86df88ad4b96ec297adcdbd8c46b751f6e6b245e4b2ea`

Limitations:

- Historical vendor firmware; not evidence of current JoshuaWire runtime compatibility.
- Exact test date, software revision, operator and reviewer are not recorded.

Provenance gaps:

- started_at: Not recorded in the original evidence; migration is not execution or validation.
- completed_at: Not recorded in the original evidence; migration is not execution or validation.
- executed_by: Not recorded in the original evidence; migration is not execution or validation.
- validated_at: Not recorded in the original evidence; migration is not execution or validation.
- validated_by: Not recorded in the original evidence; migration is not execution or validation.
- organization: Not recorded in the original evidence; migration is not execution or validation.
- review_reference: Not recorded in the original evidence; migration is not execution or validation.
- date: No test date is recorded; Git import/commit dates are not test dates.
- date_timezone: Original bench date does not specify a timezone.
- software.joshua_commit: Exact tested Joshua commit was not recorded; documentation history is not a tested commit.
- software.container_digest: Exact Docker image digest was not retained.

## am243-vendor-hello-world-undated

- [Setup](../setups/am243-vendor-hello-world-setup.json)
- [Specification](../specifications/am243-vendor-hello-world-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: Unknown
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| vendor-bringup | true | passed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/am243-vendor-hello-world.md); retained; SHA256 `9e92723db051118b1f35752cd87e6be32d25a2db07dbc87871b398993ceeabbc`

Limitations:

- Historical vendor firmware; not evidence of current JoshuaWire runtime compatibility.
- Exact test date, software revision, operator and reviewer are not recorded.

Provenance gaps:

- started_at: Not recorded in the original evidence; migration is not execution or validation.
- completed_at: Not recorded in the original evidence; migration is not execution or validation.
- executed_by: Not recorded in the original evidence; migration is not execution or validation.
- validated_at: Not recorded in the original evidence; migration is not execution or validation.
- validated_by: Not recorded in the original evidence; migration is not execution or validation.
- organization: Not recorded in the original evidence; migration is not execution or validation.
- review_reference: Not recorded in the original evidence; migration is not execution or validation.
- date: No test date is recorded; Git import/commit dates are not test dates.
- date_timezone: Original bench date does not specify a timezone.
- software.joshua_commit: Exact tested Joshua commit was not recorded; documentation history is not a tested commit.
- software.container_digest: Exact Docker image digest was not retained.

## am243-vendor-lrw-failure-undated

- [Setup](../setups/am243-vendor-ethercat-setup.json)
- [Specification](../specifications/am243-vendor-lrw-v1.json)
- Review status: unreviewed
- Tested commit: `unknown`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: Unknown
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| lrw-response | false | failed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/am243-vendor-ethercat.md); retained; SHA256 `cb3b6ea2e048a15987e86df88ad4b96ec297adcdbd8c46b751f6e6b245e4b2ea`

Limitations:

- Historical TI vendor firmware does not return the recorded LRW frames; split LRD/LWR passes separately.
- Test date, exact source/firmware, operator, root cause and relevance to current JW firmware are not established.

Provenance gaps:

- started_at: Not recorded in the original evidence; migration is not execution or validation.
- completed_at: Not recorded in the original evidence; migration is not execution or validation.
- executed_by: Not recorded in the original evidence; migration is not execution or validation.
- validated_at: Not recorded in the original evidence; migration is not execution or validation.
- validated_by: Not recorded in the original evidence; migration is not execution or validation.
- organization: Not recorded in the original evidence; migration is not execution or validation.
- review_reference: Not recorded in the original evidence; migration is not execution or validation.
- date: No test date is recorded; Git import/commit dates are not test dates.
- date_timezone: Original bench date does not specify a timezone.
- software.joshua_commit: Exact tested Joshua commit was not recorded; documentation history is not a tested commit.
- software.container_digest: Exact Docker image digest was not retained.

## Support assessments

- [ti-lp-am243-historical-assessment](../assessments/ti-lp-am243-historical-assessment.json): **historical_unreviewed**. Imported bench narratives only. No new execution, reviewer attribution or broader support qualification. Reviewer: Unknown / not recorded; time: not recorded.

## Model limitations

- Physical results apply only to the listed unit, firmware, setup and workload; untested capabilities are not qualified.
- Hardware revision and responsible maintainer are not recorded in the imported evidence.
