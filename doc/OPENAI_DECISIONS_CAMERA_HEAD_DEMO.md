# OpenAI Decisions API camera-head demo

| Field | Value |
| --- | --- |
| Status | Additional demo plan; adapter, measurements, and hardware qualification remain open. |
| Created / last reviewed | 2026-11-04 / 2026-11-10 |
| Task and rig | The same one-axis target-tracking camera head and A/B hardware from the [real-to-sim-to-real plan](CAMERA_HEAD_DEMO.md). |
| Decision backend | OpenAI Decisions API in place of the simulation-trained policy for this variant. |
| Scope | API-specific microphone input, adapter, failure handling, walkthrough, and D gates. |

## 1. Dependencies and scope

Use the camera-head plan's [task and hardware](CAMERA_HEAD_DEMO.md#2-release-scope-and-demo-setup),
[controller comparison protocol](CAMERA_HEAD_DEMO.md#controller-variants-and-comparison),
and [head artifacts](CAMERA_HEAD_DEMO.md#5-camera-head-artifacts-and-compatibility).
The nervous-system plan owns the shared [runtime and simulator contract](ROBOT_NERVOUS_SYSTEM_DEMO.md#4-shared-interfaces-and-simulator-behavior)
and [evidence record](ROBOT_NERVOUS_SYSTEM_DEMO.md#evidence-record).

Use the C0/C1 hardware, calibration, and local-motion qualification. The local
detector is needed only for a detector-state comparison condition. API-specific
work is tracked by D0–D4; R2S2R model fitting/training remains in the C gates.
Microphone input belongs only to this Decisions API variant. The conventional
and R2S2R camera-head demos keep their existing target/detector input and have no
microphone, wake-phrase, or transcription dependency.
Clear pending API work when switching sources, accepting a wake phrase for a new
target utterance, stopping, or restarting a run.

### Hardware for the first API trial

Use the shared plan's [baseline hardware and configuration handoff](CAMERA_HEAD_DEMO.md#baseline-using-existing-joshua-hardware-paths):
**one STS3215 servo + the existing SO-100-compatible USB servo bus adapter + one
USB camera A + the available Blue Yeti USB microphone + matched external servo
power + a fixed base/camera bracket + the Ubuntu Docker host**. Reuse available
parts first. The shared hardware table owns the head's parts, wiring, power, and
configuration. This plan adds the microphone and speech-input requirements below.

Joshua already implements the STS3215 motor and integrated position-feedback
drivers through `FEETECH_BUS`, USB camera capture through OpenCV, and ALSA audio
capture through `AUDIO_PUBLISHER`. Wake-phrase detection, speech-to-text, and
target selection still need integration; pin those components independently of Decisions. The head
still needs its composed command/feedback runtime, control lifecycle, preset,
calibration, and physical qualification. Pin the runtime revision supplying those
contracts before D2; this hardware choice does not close the shared runtime gates.

The initial D2 trial uses configuration A only. It needs network/API access and
recording space, but no training compute, simulation-trained policy, second motor,
Teensy/ESP32, lidar, VL53L5CX, or DYNAMIXEL integration. Camera B and its bracket
enter D3 when demonstrating the A→B hardware change; they remain required for the
full R2S2R A/B release. Optional devices stay outside the first API trial.

Use STS3215 throughout this demo, including the camera A→B change. XL330
integration is deferred to later work and is not required for D0–D4.

Before D2, retain a hardware record with exact servo/adapter/camera/microphone/supply
models and revisions, servo ID/baud and USB mappings, wiring/polarity, payload measurements,
calibrated direction/zero/limits/speed, actual image and feedback rates/ages, and
local stop/link-loss results. Reference the authoritative protobuf config and
calibration snapshot. Freeze the bounded pan step, freshness limits, and API
cadence at D0 against these measurements rather than copying SO-100 motion values.

### Always-listening microphone input

Use the available **Blue Yeti USB microphone**, mounted on the desk/base rather
than the moving camera bracket. Confirm the exact model and capture compatibility
at D0/D2; the unit has not yet been tested. Keep it unchanged across camera A/B
trials. Connect it by USB to the Ubuntu host and provide Docker `/dev/snd` access
and permissions.

Reuse the [audio preset](../config/config_preset/example/publish_audio.pbtxt) and
[audio publishing contract](../ros2/README.md#audio-publishing): `AUDIO` with
`alsa_audio_config` and `AUDIO_PUBLISHER` publishes serialized
`PerceptionPacket.audio` in `UInt8MultiArray` on `/audio/pcm`. Select a verified
`plughw:CARD=...,DEV=...` mapping; do not assume a Yeti card name or use `default`.
Start qualification with the example's 16 kHz mono, 320-frame chunks (20 ms),
verifying ALSA plug conversion on the actual unit. Record native format,
resampling/downmixing, capture buffer, USB identity, gain/pickup setting, and
topic/QoS. One capture owner supplies all audio consumers.

While this demo's listening mode is active, capture continuously and run a
**local wake-phrase detector** for **“Joshua”**. A visitor says **“Joshua, follow
the red target”** without pressing a button. Only an accepted wake event opens
a bounded command utterance; voice activity detection ends it after the configured
silence interval, with a maximum duration and transcription deadline. Keep a
bounded local pre-roll so the first command words are not clipped. Background
audio outside a wake-triggered utterance is not sent to a transcription service.
Show listening, command capture, transcribing, accepted target, and rejection
states, with a local mute/listening control.

Transcribe the utterance, remove the wake phrase if present, and validate a
nonempty target-selection command against the D0 grammar and target vocabulary.
For example, “follow the red target” supplies the description “red target” to
Decisions alongside the camera image and measured joint state. Display the
transcript and accepted description. Treat speech as task data; it cannot rewrite
the question choices, enable motors, or change motion limits. Decisions interprets
the selected visual target. Sound-source localization and general conversation
are outside this demo. Typed fallback uses the same target validation path.

Pin wake detector/model/threshold, speech backend/model/language, endpointing,
pre-roll, utterance bounds, deadlines, target grammar, and recovery settings in
the composed protobuf preset and referenced artifacts at D0. Capture fields
already exist; speech schema and consumers remain implementation work. Keep
credentials outside checked-in configuration. Bound transcription work to one
active utterance/request; suppress repeated wake triggers until it completes or
expires. Re-arm after the configured cooldown rather than queueing background speech.

An accepted wake event suspends tracking increments, applies the qualified local
hold/stop behavior, and advances the target generation to invalidate old work.
Silence without a wake event leaves an existing target active. After waking,
empty/ambiguous/unsupported speech, timeout, or audio discontinuity leaves tracking
suspended and asks for a fresh wake phrase and command. Discard partial utterances
on sequence gaps, microphone disconnect, or restart. Tag speech results with
run/config/target-generation identity and reject late results after stop, source
change, or reset. Microphone outage or mute suspends tracking; recovery requires
deliberate operator action and fresh input. Speech never substitutes for the
independent operator enable/stop controls.

Record wake events, utterance/target IDs, transcript acceptance/rejection, audio
gaps, false/missed wake events, and speech-to-target latency separately from
tracking results. Use fixed utterance fixtures and target sequences for comparison,
preserving failed interactions. Replay audio and stub transcription responses for
hardware-free, network-free checks. These speech requirements belong to D0–D4;
they add no prerequisite to the shared C gates or non-Decisions demos.

## 2. Observation → decision → bounded movement

The [official Decisions guide](https://developers.openai.com/api/docs/guides/decisions)
documents `POST /v1/decisions`, image and JSON evidence, and `choice` questions
that return a supplied option with probabilities and confidence. As reviewed on
2026-11-04, the documented supported model is `gpt-6-luna`; verify availability
and pin the model/SDK when implementing. The application executes selected actions.

```mermaid
flowchart LR
    Mic[Continuous Blue Yeti audio] --> Wake[Local wake phrase detection]
    Wake --> Speech[Bounded utterance and transcription]
    Speech --> Target[Validated target description and generation]
    Target --> API[OpenAI Decisions API]
    Snapshot[Recent frame and measured joint snapshot] --> API[OpenAI Decisions API]
    API --> Gate[Local freshness and decision checks]
    Gate --> Adapter[Calibrated bounded pan adapter]
    Adapter --> Owner[Joshua hardware owner]
```

1. Accept a spoken target such as “Joshua, follow the red target” through the
   wake-phrase flow above. Pass the validated target description as task data; keep
   the question choices, limits, and control authority fixed. Raw microphone
   audio is handled by the speech stage, not sent as Decisions evidence.
   Once the target is accepted, select a recent camera frame and measured joint
   state. Record their sample IDs, acquisition/receipt times, clock domains, and
   alignment quality. Give the local request a run/config identity,
   target-generation ID, and observation ID for correlation.
2. Send the image, the target description, and the aligned joint state to Decisions.
   Pin the image preprocessing and the centered-image band in the question contract.
3. Ask one named `choice` question with the following mutually exclusive options.
   These are project-defined labels, not built-in API actions:

| Choice | Meaning | Local action |
| --- | --- | --- |
| `target_left` | Target is visible left of the centered band. | Request one bounded pan increment toward that image direction. |
| `target_centered` | Target is visible inside the centered band. | Hold the tracking target; no new pan increment. |
| `target_right` | Target is visible right of the centered band. | Request one bounded pan increment toward that image direction. |
| `target_absent` | Target is not visible in this frame. | Apply the qualified target-loss hold/stop policy; no searching motion. |
| `uncertain` | Visibility or location cannot be determined reliably. | Apply the local hold/stop policy. |

4. Validate the returned question name, answer type, selected option, and finite
   confidence/probabilities. The [create-decision reference](https://developers.openai.com/api/reference/python/resources/decisions/methods/create)
   also permits a refusal instead of an answer; handle that without issuing motion.
5. Use the calibrated camera-to-joint sign to map an accepted left/right choice
   to a bounded angular increment from fresh measured position. Keep step size,
   position/speed limits, command lifetime, and deadband in the protobuf config
   and referenced calibration. The API does not choose raw servo registers,
   enable state, or unrestricted angles.
6. Record the observation, request/response, accepted or rejected decision, applied
   command, measured response, and timing. Show decision, confidence, frame age,
   response delay, and actual joint angle in the audience view. Also show the
   transcript, accepted target, and speech-to-target delay separately.

Implement this as an asynchronous decision source feeding Joshua's local control
path. Camera acquisition, command supervision, recording, and operator stop must
continue while the request is in flight. The API request cadence and the local
control cadence are separate settings, qualified through measurement.

### Freshness, failures, and recovery

Allow at most one API request in flight; keep a bounded latest-snapshot slot for
the next request instead of accumulating frames. Accept an answer only while its
input age, joint-state age/skew, and motion since the input remain within the
D0 bounds, and its run/config/control-source and target-generation identities
still match. Consume an accepted observation ID at most once. A newer camera frame alone does not
invalidate an otherwise timely answer; a superseded or expired decision does.

Set a finite request deadline and retry budget, including SDK retries. Timeouts,
network loss, rate limits, refusal, malformed answers, insufficient confidence,
stale observations, or target loss stop new tracking increments and invoke the
qualified local hold/stop policy. A previous decision must not keep generating
movement. Select confidence thresholds from labeled task examples; model
confidence alone does not establish correctness or physical readiness.

An accepted wake event invalidates pending Decisions work and suspends tracking
under this demo's speech-input contract. After acceptance, take a new image/joint
snapshot for that target; do not reuse a decision made for the previous target.
Speech rejection, timeout, or microphone outage follows the speech hold/recovery
rules without blocking the local operator stop.

Reject late responses after target replacement, stop, backend switching, or an
A→B change. API recovery follows the shared lifecycle and cannot automatically
enable an actuator.

## 3. API audience walkthrough

For the additional presentation segment:

1. Show the same head and target, then select the Decisions backend while stopped.
2. With listening active, say “Joshua, follow the red target” into the Blue Yeti. Show the
   transcript and validated target, then explicitly enable through the operator
   control. Display a recent frame, returned choice/confidence, and bounded applied
   movement beside measured angle. Identify the transcription backend and live
   Decisions dependency; show the wake indicator, rejected speech, and a labeled
   typed fallback. Verify background conversation without a wake event leaves
   the selected target unchanged.
3. Move the target through the frozen trajectory and demonstrate centering and
   target loss. Show recorded benchmark results alongside the live interaction.
4. Demonstrate a prescribed API outage or delayed reply. The local runtime must
   follow its qualified hold/stop behavior and reject the expired response.
5. Optionally repeat the A→B camera/payload change. Update calibration and prompt
   inputs as needed; qualify behavior again and report those changes separately
   from training effort.

Keep a labeled recorded fallback for unavailable network/API service. Publish
whether a segment used live Decisions, recorded answers, or another controller.
Also label live microphone/transcription, audio replay, and typed target input.

## 4. Implementation and acceptance gates

| Gate | Work and required evidence | Status |
| --- | --- | --- |
| D0 — Freeze the API contract | Select API access/model/SDK, target/preprocessing, choices, bounded step/deadband, request cadence/deadline/retry budget, response freshness/motion/confidence bounds, and request/spend budget. Pin this demo's speech contract: Yeti capture mapping, local wake detector/model/threshold, transcription backend/model/language, endpointing/pre-roll/cooldown, utterance bounds, grammar/vocabulary, target generations, false/missed wake and latency/acceptance criteria, and any transcription service budget. Freeze the baseline hardware record and reference the qualified head calibration, local hold/stop behavior, and shared trial protocol. Keep credentials outside source-controlled configuration. | Open |
| D1 — Adapter and replay | Add config/schema, local wake detection and bounded utterance assembly, asynchronous transcription and API adapters, target validation, local mapping/gates, source selection, and recording/display integration. Hardware-free audio/transcript/decision fixtures cover silence/noise without activation, false/missed/repeated wake events, clipped/overlong utterances, mute/re-arm, unsupported targets, capture gaps, speech timeout, target replacement and old-target replies, malformed/refused/low-confidence answers, stale and duplicate IDs, delayed/out-of-order completion, source changes, rate limits, and outages. | Open |
| D2 — Bounded physical qualification | On the existing qualified head, verify continuous Yeti capture, wake detection and spoken target selection under demo noise, false/missed wake behavior, mute/re-arm, rejection/replacement, microphone unplug/recovery, typed fallback, direction/limits, tracking, target loss, operator stop during transcription, API failure/deadline behavior, and rejection of late responses. Record all attempts and measured speech and control timing separately. | Open |
| D3 — Comparison and supported change | Apply the shared camera-head comparison protocol and record API-specific provenance/usage. Repeat the shared camera A→B change with the STS3215 actuator when claiming easy hardware changes. XL330 integration is outside these gates. | Open |
| D4 — Conference readiness | Pin config/calibration/prompt/model/SDK and speech-input artifacts, rehearse live microphone input plus labeled typed/audio-replay and API fallback, and publish the runbook and D0–D3 evidence. | Open |

Record prompt/preprocessing revisions, model/SDK identity, request correlation,
responses/rejections, usage/cost, and applied commands in the shared evidence
record. D gates establish this API path; C gates retain the trained-policy claims.
The general-audience read-only inspection walkthrough remains a separate activity.
Follow [AGENTS.md](../AGENTS.md) for execution authorization. No API adapter or
hardware result is established by this plan.
