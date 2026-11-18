# Sunplus Innovation Technology Inc. XWF-1080P

Generated from canonical catalog records. Historical results apply only to the listed setup and specification.

- Hardware ID: `sunplus-spca2281-xwf-1080p`
- Kind: camera
- Integration: intended
- Scope: Image capture through the existing OpenCV/V4L2 driver using the example preset on Linux.
- Capabilities: image-capture
- Revisions: Unknown / not recorded

## Model and setup references

- [Structured model](../models/sunplus-spca2281-xwf-1080p.json)
- [Operating guide](../../../robot/perception/AGENTS.md)

## Validation history

| Run / specification | Result | Date / actual time | Executed by | Validated by / time |
| --- | --- | --- | --- | --- |
| [sunplus-spca2281-2026-11-04-humble-1080p-opt-300s](../runs/sunplus-spca2281-2026-11-04-humble-1080p-opt-300s.json) / camera-1080p-delivery-v1 v1 | passed | 2026-11-04 / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |
| [sunplus-spca2281-2026-11-04-humble-640x480](../runs/sunplus-spca2281-2026-11-04-humble-640x480.json) / camera-640x480-capture-v1 v1 | passed | 2026-11-04 / exact time unknown | Unknown / not recorded | Unknown / not recorded / not recorded |

## sunplus-spca2281-2026-11-04-humble-1080p-opt-300s

- [Setup](../setups/sunplus-spca2281-2026-11-04-humble-1080p-opt-300s-setup.json)
- [Specification](../specifications/camera-1080p-delivery-v1.json)
- Review status: unreviewed
- Tested commit: `261887dfdcfd9645b846ecffe3a3f30cfb3f111b`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: America/Los_Angeles
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| capture | true | passed |
| mean-rate | 30.003333 | passed |
| window-rate | 29.9 | passed |
| max-gap | 56.313 | passed |
| invalid-frames | 0 | passed |
| stamp-order | 0 | passed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/sunplus_spca2281_1080p_2026-11-04.md); retained; SHA256 `ecefdecf8b78e5dde553bce3924678ef53f9a9997bde3aeef721a5b58d144d7c`

Limitations:

- A five-minute run on one physical unit and one Ubuntu 22.04/Humble setup; not an endurance or cross-platform qualification.
- This result requires the two-buffer capture change and an optimized build. With two buffers, the default development build measured 23.2 FPS in the short comparison.
- The recorded Joshua commit is the source base; the driver change and new preset were uncommitted during measurement. Their exact SHA-256 hashes are retained in the environment.
- Frame rate and gaps were measured at the ROS subscriber. Publisher timestamps do not establish camera-exposure timing, synchronization accuracy, or delivery of every sensor exposure.
- The final automatic-exposure settings matched the original settings. Other lighting conditions and hardware revisions were not qualified.

Provenance gaps:

- started_at: Not recorded in the original evidence; migration is not execution or validation.
- completed_at: Not recorded in the original evidence; migration is not execution or validation.
- executed_by: Not recorded in the original evidence; migration is not execution or validation.
- validated_at: Not recorded in the original evidence; migration is not execution or validation.
- validated_by: Not recorded in the original evidence; migration is not execution or validation.
- organization: Not recorded in the original evidence; migration is not execution or validation.
- review_reference: Not recorded in the original evidence; migration is not execution or validation.
- software.container_digest: Exact Docker image digest was not retained.

## sunplus-spca2281-2026-11-04-humble-640x480

- [Setup](../setups/sunplus-spca2281-2026-11-04-humble-640x480-setup.json)
- [Specification](../specifications/camera-640x480-capture-v1.json)
- Review status: unreviewed
- Tested commit: `74ecbcc618658358de2bbc9048fe9d60df7acadb`
- Started: Unknown; completed: Unknown
- Execution timezone/date context: America/Los_Angeles
- Organization: Unknown
- Review: Not recorded

| Check | Observed | Outcome |
| --- | --- | --- |
| capture | true | passed |

Evidence:

- [Retained historical test narrative](../../../docs/hardware_catalog/evidence/sunplus_spca2281_2026-11-04.md); retained; SHA256 `792f87e379972a8e4f528d4736692ccef2e70e8b57796d9022644155b2134b36`

Limitations:

- Short capture and desktop preview on one physical unit; no endurance or cross-platform qualification.
- Requested 30 FPS; measured 8.46 FPS with a reliable ROS subscriber. Direct OpenCV capture also achieved approximately 8 FPS.
- 1080p capture was not tested. The model name and advertised modes are not verification evidence.
- The preset uses camera index 0; resolve the connected device before running because indices can change.
- USB VID:PID and model aliases identify a candidate model, not a unique physical unit or a guarantee for every hardware revision.

Provenance gaps:

- started_at: Not recorded in the original evidence; migration is not execution or validation.
- completed_at: Not recorded in the original evidence; migration is not execution or validation.
- executed_by: Not recorded in the original evidence; migration is not execution or validation.
- validated_at: Not recorded in the original evidence; migration is not execution or validation.
- validated_by: Not recorded in the original evidence; migration is not execution or validation.
- organization: Not recorded in the original evidence; migration is not execution or validation.
- review_reference: Not recorded in the original evidence; migration is not execution or validation.
- software.container_digest: Exact Docker image digest was not retained.

## Support assessments

- [sunplus-spca2281-2026-11-04-humble-1080p-opt-300s-assessment](../assessments/sunplus-spca2281-2026-11-04-humble-1080p-opt-300s-assessment.json): **historical_unreviewed**. Recorded capture result for this setup only; no newly attributed review or model-wide qualification. Reviewer: Unknown / not recorded; time: not recorded.
- [sunplus-spca2281-2026-11-04-humble-640x480-assessment](../assessments/sunplus-spca2281-2026-11-04-humble-640x480-assessment.json): **historical_unreviewed**. Recorded capture result for this setup only; no newly attributed review or model-wide qualification. Reviewer: Unknown / not recorded; time: not recorded.

## Model limitations

- Physical results apply only to the listed unit, firmware, setup and workload; untested capabilities are not qualified.
- Hardware revision and responsible maintainer are not recorded in the imported evidence.
