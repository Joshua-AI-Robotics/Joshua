# Policy interface and execution bridges

Status: **Proposed implementation plan; no runtime changes in this document.**

Prepared 2026-11-09 against `develop` at `68605e76`. The separate
`kangjin/spider-joshua-integration` branch at `9ab34b7a` is a design reference,
not an implemented dependency on this branch.

## 1. Outcome and scope

Run a compatible policy through Joshua against MuJoCo, Isaac Sim, or physical
hardware without changing the policy's observation and action meanings. Support
both locomotion and vision-language-action (VLA) policies. Training may happen
in mjlab, Isaac Lab, LeRobot, or another external workflow; the runtime contract
must not depend on which trainer produced the policy.

Extend the existing inference plug-in architecture. Joshua owns typed data,
backend bindings, lifecycle, timing enforcement, and command routing. Plug-ins
own model loading, preprocessing, normalization, inference, and postprocessing.
Each backend supplies its own bridge. Each training workflow supplies its own
export integration.

Interface reuse does not imply weight reuse. Policies declare their compatible
robot structure, observations, actions, and operating envelope. Two units of the
same design may share weights after calibration and validation; a different
morphology may require a new policy and simulation model.

This plan does not build a universal trainer, automatically generate accurate
physics from a hardware preset, impose one observation vector on all models,
or qualify physical deployment. It does not introduce MCP into the control loop.
The first delivery is a small contract and one executable simulation path;
Isaac, action chunks, and hardware follow independently.

### User benefit: faster integration and iteration

The intended benefit is less repeated integration work after the first working
setup. Users can change a policy or execution backend while reusing state
assembly, command routing, lifecycle handling, and diagnostics. This is a
development-speed hypothesis to verify, not a measured speedup today.

| User task | Reusable work | Work the user still does |
| --- | --- | --- |
| Try a new policy on an existing robot | Sensor acquisition, command routing, reset, timing checks, and diagnostics | Load the model, declare requirements, and implement model-specific input/output conversion |
| Evaluate in another simulator | Policy adapter, semantic contract, and evaluation/reporting mechanisms | Supply a compatible robot model, configure the bridge, and validate engine-specific behavior |
| Move from simulation to hardware | Policy adapter and named observation/action meanings | Bind real streams, calibrate, validate timing and dynamics, and qualify physical behavior |
| Compare policies | Recorded inputs, execution path, and evaluation procedure | Select compatible candidates and task-specific success criteria |
| Diagnose a failure | Independent policy replay and bridge tests | Identify whether the problem is the model, mapping, timing, or physical response |
| Add another unit of the same robot design | Compatible policy bundle and integration code | Configure endpoints and calibrate and validate the new unit |

For example, the intended spider workflow is to configure joint identities,
sensors, calibration, limits, and model references; train in mjlab and export
the complete controller; select the bundle in a Joshua preset; validate its
requirements and evaluate through the MuJoCo bridge; then bind the qualified
hardware path using the same policy adapter. Compatibility checks should expose
missing measurements or mismatched units before execution. This workflow
depends on the planned bridges and hardware prerequisites being implemented.

Robot modeling, reward design, training, new drivers, and hardware qualification
remain necessary. The first integration also pays the cost of developing the
shared contract. Savings should come from subsequent integrations reusing that
work, rather than from skipping validation or treating a new morphology as a
configuration-only change.

## 2. Existing foundations and gaps

| Area | Present on the baseline | Work still required |
| --- | --- | --- |
| Policy loading | [InferenceAdapter](../ai/inference/adapter.py), manifest registry, per-model environments | Versioned semantic requirements and compatibility validation |
| Runtime inputs/outputs | [Observation and ActionCommand](../ai/inference/types.py); images and scalars; scalar output indexed by publisher | Named structured observations, named commands, source timing, command identity and validity |
| Scheduling | [InferenceHost](../ai/inference/host.py) supports EVENT and TICK | Bounded inference, explicit reset, missed deadlines, and action lifetime; a timer alone is not a real-time guarantee |
| VLA | [SmolVLA adapter](../ai/models/smolvla/adapter.py) processes images, scalar state history, and configured task text | Explicit joint identity and input freshness; optional shared action-chunk execution |
| MuJoCo | [Simulation modes](../simulation/README.md) provide viewing, playback, mirroring, and rendering | Reusable policy execution bridge |
| Isaac Sim | [Subprocess launcher](../simulation/isaac/launcher.py) and viewer | Bidirectional policy execution, capability handshake, and reset/restart handling |
| Hardware | [C++ robot layer](../robot/README.md), named SI joint commands, motor and sensor drivers | Policy state assembly and command mapping, plus qualification of each selected hardware path |
| IMU | Interface/type placeholders; [factory](../robot/perception/factory/perception_factory.cc) still reports no IMU driver | Driver, timestamped acquisition, calibrated frame transform, and orientation/gravity estimate where required |
| Time | [RobotTime](ROBOT_TIME.md), packet timestamps, separate device/receipt metadata for scalar streams | Explicit inference clock domains, simulation time, and verified acquisition alignment |
| Training | [Data collection](../ai/train/README.md) on this baseline | External training/export recipes and deployment bundles |

The spider branch demonstrates useful requirements: complete state frames,
episode/step correlation, a frozen hybrid controller, and replay comparisons.
Its 33-value state, 47 actor inputs, twelve outputs, and 20 ms period remain
policy-specific. Do not turn them into Joshua-wide constants or require its
Float32MultiArray protocol for other policies.

The [shared runtime demo plan](../doc/ROBOT_NERVOUS_SYSTEM_DEMO.md#4-shared-interfaces-and-simulator-behavior)
owns execution-target selection, source ownership, and hardware lifecycle
requirements. The [camera-head plan](../doc/CAMERA_HEAD_DEMO.md) owns its
real-to-sim-to-real experiment. This plan supplies reusable policy mechanisms
for those consumers; it does not create competing enable/stop state machines.
Reconcile implementation status with source when taking up each milestone:
older plan backlogs are not an inventory of current merged support.

## 3. Architecture and responsibility boundaries

```mermaid
flowchart LR
    Config[Protobuf preset and artifact references] --> Bind[Validate and bind]
    Train[External trainer and exporter] --> Bundle[Versioned policy bundle]
    Bundle --> Bind
    Bind --> Runtime[Joshua policy runtime]
    MJ[MuJoCo bridge] -->|Observations| Runtime
    IS[Isaac Sim bridge] -->|Observations| Runtime
    HW[Hardware ROS bridge] -->|Observations| Runtime
    Runtime <--> Plugin[Policy plug-in]
    Runtime -->|Named commands| Route[Selected execution target]
    Route --> MJ
    Route --> IS
    Route --> HW
    HW <--> Nodes[Existing C++ hardware nodes]
```

Only the selected execution bridge receives commands. A separate read-only
mirror may observe hardware while it runs; restarting that mirror must not
take command ownership or reset hardware. Failure of the selected execution
bridge instead invalidates its policy session and invokes the configured fault
behavior. Replay, mirroring, simulated execution, and hardware execution have
distinct source labels and configurations.

| Component | Owns | Does not own |
| --- | --- | --- |
| Policy plug-in | Tensor layout, normalization, image preparation, task encoding, recurrent state, gait/controller logic, action decoding | ROS endpoints, physics stepping, physical device access |
| Shared policy runtime | Binding validation, bounded state assembly, scheduling, inference request identity, output validation, lifecycle and diagnostics | Robot-specific gait or reward functions |
| Simulation bridge | Engine names/indices, frame/unit conversion, simulated observations, action application, stepping/reset | Learned weights or model-specific normalization |
| Hardware ROS bridge | Bind configured streams and named commands; integrate with existing control ownership and fault handling | Opening serial/NIC devices or replacing C++ drivers |
| Estimator/preprocessor | Declared derived measurements such as body gravity direction or joint velocity, with validity and timing | Pretending estimated state is direct sensor feedback |
| Exporter | Freeze the complete controller and its dependencies, provenance, and reference input/output fixtures | Changing live wiring or silently overriding calibration |

Keep pure contract and scheduling helpers under `ai/inference/`; keep model
logic under `ai/models/<model>/`. Engine-specific code belongs under
`simulation/mujoco/` and `simulation/isaac/`. ROS codecs and hardware routing
integrate with `ros2/`; no Python hardware drivers are added under `robot/`.
Concrete filenames and proto field numbers are chosen in the implementing PRs.

## 4. Proposed contract

The names below describe proposed records, not an API already available.
Use protobuf for static configuration and serialized metadata. Use ROS-free
Python records for decoded runtime values, following the existing architecture.

### Minimum interface and optional capabilities

The mandatory interface for a versioned adapter is limited to declaring its
requirements, accepting named typed inputs, returning named commands, and
participating in initialize/reset/close lifecycle. Shared envelopes preserve
identity, units/frames, validity, and timing; command admission validates those
semantics and rejects incompatible or expired outputs. These checks apply even
when an adapter uses no optional execution helpers.

Compose additional behavior only when a consumer needs it:

| Capability | When to add or select it |
| --- | --- |
| Multi-source state assembly | A policy needs inputs from separate streams with declared age/skew rules; an already complete frame needs no extra assembler. |
| Periodic scheduling | A policy requires a control cadence; event-driven policies do not implement a periodic hook. |
| Bounded asynchronous inference | Model execution must be isolated from ingestion and control callbacks; direct lockstep evaluation can use synchronous calls with a defined timeout boundary. |
| Action-chunk execution | A concrete policy produces timed sequences; single-action policies use the ordinary command path. |
| Estimation and resampling | A required measurement needs a declared, validated transformation; select the component explicitly. |
| Temporal history helper | Two concrete policies demonstrate the same buffering semantics; otherwise history stays in the plug-in. |

Requirements describe only the capabilities an adapter uses. Unsupported
capabilities are rejected at binding rather than replaced with no-op hooks.
Do not require every plug-in to implement all scheduling modes or allocate
queues for capabilities it does not use. Extraction into shared helpers follows
working examples; the records and modes below are a staged design, not a
requirement to implement the entire framework in P1.

### Policy requirements and backend capabilities

Extend `AdapterSpec` with an explicitly versioned, opt-in semantic contract.
Unversioned adapters retain their existing behavior. A new adapter declares:

- Required and optional logical input names, payload schemas, units, coordinate
  frames, shapes or bounded dimensions, and measured/estimated/simulated status.
- Joint identities and supported output modes: initially absolute joint position;
  velocity, effort, Cartesian goals, and other modes require separate consumers.
- Scheduling mode, nominal control period where applicable, inference deadline,
  input age/skew limits, and allowed missing-input behavior.
- Supported reset behavior, temporal history, and whether outputs are single
  actions or bounded chunks.
- Robot/model compatibility requirements and required estimator definitions.

A bridge advertises what it can actually provide and consume. Resolve logical
names to configured endpoints and simulator joints once at binding time.
Reject missing inputs, duplicate names, unsupported modes, unit/frame mismatch,
or incompatible artifacts before admitting commands. Shape equality alone is
insufficient. Do not silently replace missing measurements with zero or ground
truth. Validate bounds and dynamic availability again while running.

### Observation samples and assembled input

Extend the current input representation with typed, named payloads and an
envelope containing source identity, sequence, runtime/session identity,
source time and clock domain when available, receipt time and clock domain,
validity, and frame/unit metadata. Preserve the origin of derived values and
their input sample identities. Own or copy payload memory so a later sensor
update cannot mutate an input already submitted for inference.

Start with joint state, image, and task-text schemas. Add IMU and derived
orientation/gravity schemas alongside the corresponding bridge/estimator work.
Unknown acquisition time remains unknown. Receipt freshness cannot establish
acquisition freshness. Do not subtract unrelated device, host, or simulation
timestamps; reuse [RobotTime's limitations](ROBOT_TIME.md) explicitly.

`ObservationBatch` groups the samples used for one inference request and records
their individual timestamps. It does not imply simultaneous acquisition. The
runtime applies the adapter's declared maximum age, skew, missing-data, and
resampling rules. History remains plug-in-owned unless two concrete policies
demonstrate a reusable history helper. Optional inputs have explicit availability;
their absence must not change tensor layout implicitly.

Share metadata semantics with the [observation reader design](MCP_OBSERVATION_DESIGN.md),
but do not use its read-latest MCP interface as the high-rate control scheduler
or make inference depend on `tools/mcp`.

### Commands and action chunks

Introduce a named `JointCommandBatch` carrying session/generation, command ID,
originating observation/request ID, target joint names, explicit command mode
and units, and a bounded validity interval with a defined clock domain. Require
finite values, unique names, correct dimensions, supported modes, and configured
limits before accepting the complete batch. Start with absolute SI position
targets; revolute joints use radians. Other joint types need explicit units and
verified driver support.

Keep model normalization and relative-to-absolute conversion inside the plug-in
or an explicitly selected converter. Driver calibration remains authoritative
for physical native-unit conversion. Apply each conversion exactly once. Reuse
the existing `JointCommand`/`POSITION_SI` path; never publish radians to legacy
scalar native-unit topics by assuming they mean the same thing.

Acceptance of a complete command does not guarantee simultaneous physical
application across boards. Report validation, transport acceptance, application
acknowledgment where available, and measured response separately. Unknown
application status stays unknown. A communication heartbeat must not refresh an
expired policy target.

`ActionChunk` is a later extension containing a bounded sequence of named
commands and relative execution offsets. Define maximum horizon, queue capacity,
replacement/preemption, and interpolation explicitly. Discard expired or
superseded chunks; clear the queue on reset, stop, task change, and session loss.
A single-step controller must not be forced to implement chunking.

### Adapter lifecycle and scheduling

Preserve registry loading and per-model environments. Add an opt-in lifecycle
extension for versioned adapters: pre-load requirements inspection, binding,
initialization, reset with a new generation, inference, and close. Retain
`on_observation`/`on_tick` for legacy adapters through a compatibility path;
do not reinterpret their timestamp or normalization semantics in place.

| Execution mode | Required behavior |
| --- | --- |
| Simulation lockstep | Bridge produces state for one source step; applies one matching complete command before advancing physics. Sim time controls policy phase; monotonic wall time bounds transport waits. |
| Live periodic control | Runtime samples an admissible input batch and requests an action on the configured cadence. Reject late results; do not issue a burst of missed ticks to catch up. |
| Event-driven inference | A declared input event triggers inference only when its required input set is admissible. Bound queued work and define supersession. |
| Chunked execution | Inference may run slower than action execution. A separate bounded executor validates each action's lifetime and the selected replacement rule. |

Give one scheduler authority per execution session. Keep expensive inference
off the ROS callbacks responsible for stop, health, and input ingestion. Start
with one in-flight request per policy session and a bounded pending-input slot.
A timed-out worker may finish later; request/generation checks prevent that
result from reaching the command sink. Worker cancellation and shutdown must be
bounded, using process isolation if the model cannot cooperate.

Reset must clear model history, gait phase, queued inputs/actions, and request
identity before the new generation can emit commands. Readiness requires valid
binding and required observations, not merely successful weight loading.
For hardware, stop/enable/recovery is delegated to the existing or planned
shared runtime control lifecycle. No automatic re-enable follows reconnect.

## 5. Bridge implementations

### MuJoCo

Build a policy execution mode around the existing engine. Map joints by name,
explicitly distinguish link and inertial frames, reconstruct only declared
observations, and apply targets through the matching actuators. Physics timestep
and controller timestep are separate; validate their stepping relationship.
Expose reset and simulation epoch/step identity. Optional wall-clock pacing
must not change the meaning of simulation time.

Support direct in-process calls first, then ROS transport with identical
semantic records. Both paths run the same compatibility and output validators.
Use an explicit, versioned wire envelope with integer sequence/generation IDs;
the spider prototype's float-array IDs are not the general protocol.

### Isaac Sim

Keep the existing subprocess/dependency boundary. Add a bridge inside the
selected Isaac environment that publishes observations and accepts named
commands through the shared ROS transport contract. If ROS dependencies require
another relay process, document ownership and test that additional boundary.

Implement startup capability exchange, joint/frame/unit mapping, physics/control
cadence, simulation clock, reset, timeout, and process-loss behavior. Pin the
tested engine version in the integration recipe. A restarted simulator creates
a new session; old replies and commands cannot apply to it.

Validate semantic equivalence on controlled fixtures. Do not require identical
MuJoCo and Isaac trajectories: their physics and actuator models may differ.
Camera observations are supported only after rendering, encoding, calibration,
and timestamps have been implemented and tested on that bridge.

### Physical hardware

Consume the configured ROS feedback streams and send named SI commands through
the existing C++ action path. Preserve a single owner for each physical bus.
Confirm command/feedback concurrency for the selected servo path; existing
mixed-MCU support does not prove every motor bus already supports that topology.

The IMU work must expose calibrated body angular velocity and, when required,
an orientation/gravity estimate with timing and validity. Raw accelerometer
measurements are not interchangeable with the policy's projected gravity input.
Joint velocity must have a defined measured or estimated source. A command
echo or open-loop position estimate is not encoder feedback.

Before hardware rollout, the selected runtime must enforce command-source
ownership, age/expiry, limits, explicit enable, and fault recovery. Select and
qualify the per-robot fault action; neither holding the last target nor disabling
torque is universally appropriate. Do not infer physical stop guarantees from
host-side rejection or software acknowledgments. Hardware runs remain governed
by [AGENTS.md](../AGENTS.md) and the selected device qualification procedure.

## 6. Configuration, artifacts, and training integration

The `.pbtxt` preset remains the source of truth for wiring, backend selection,
calibration references, allowed command sources, operational bounds, policy
selection, and artifact paths. Extend existing schemas where needed; introduce
no parallel robot YAML/JSON configuration system.

A deployment bundle contains weights, normalization, model-specific controller
assets, contract version, expected input/output semantics, required runtime
dependencies, robot-model/calibration provenance, and reference inference
fixtures. Extend existing model manifests or reference a versioned artifact
manifest from them rather than create a second plug-in registry. Frozen training
assumptions are evidence to validate against the preset, not live wiring overrides.

Record hashes and versions for assets and training provenance. Define separately
which changes require an exact match, revalidation within a declared envelope,
or retraining. Do not use a serial port or robot-unit identifier to require
retraining when only endpoint wiring changed. A changed joint order, observation
meaning, control period, or normalization cannot be accepted just because the
array dimensions match.

| Integration | Export responsibility |
| --- | --- |
| mjlab/PPO | Complete locomotion controller, including gait/feedback outside the neural network; recorded observation/action comparisons |
| Isaac Lab or another RL trainer | Equivalent semantic contract and exporter for its own model/controller; no assumed checkpoint-format compatibility |
| LeRobot/VLA | Model and processor versions, camera/task/state requirements, normalization, and action/chunk interpretation |

Keep GPU training batches and simulator-native tensors inside the trainer. Share
contract definitions and conversion fixtures with Joshua; do not route each PPO
environment step through ROS or import training libraries into the core runtime.
NumPy, PyTorch, or ONNX deployment is a plug-in choice, not a universal format.
Recorded replay establishes export agreement on those inputs; closed-loop
simulation and physical validation remain separate evidence.

## 7. Delivery sequence and acceptance gates

Each row is a focused implementation increment with its own tests and docs.
All are **open**. This planning branch implements none of them.

P1 and P2 use two small deterministic policy fixtures from the start: a
locomotion-style controller consuming named state at a fixed cadence, and a
VLA-style controller consuming images, named joints, and task text on events.
They require no trained weights or GPU and exercise the same contract with
different input and scheduling needs. Fixtures establish interface behavior,
not model quality. Revise the provisional contract from both examples before
expanding the framework; P5 later validates the design with actual models.

| Step | Deliverable and likely areas | Dependencies | Acceptance gate |
| --- | --- | --- | --- |
| P1 — Minimum contract and two fixtures | Versioned requirements, sample metadata, named position commands, preflight validation in `ai/inference/` and existing protos; locomotion and VLA fixtures | None | Both fixtures use the same minimum interface without model-specific host branches. Pure tests reject wrong names/units/frames/modes, malformed data, incompatible versions and artifact assumptions. Contract imports need no simulator or model SDK. Existing presets/adapters retain behavior. |
| P2 — Demonstrated execution helpers | Extract only the state assembly, generation/reset, bounded scheduling and diagnostics needed by the two fixtures; integrate `InferenceHost` | P1 | Both fixtures run with their selected capabilities and no unused mandatory hooks. Controlled-clock tests cover skew/age, unknown clocks, timeout, late completion, restart, shutdown, and no post-stop publication. Model-loading exceptions cannot enable output. Compare direct and shared-path overhead for numeric and image inputs. |
| P3 — MuJoCo direct path | Generic policy mode, named mappings, lockstep control, one small deterministic reference policy | P1–P2 | Hardware-free preset completes repeatable episodes; correct reset, permuted joint storage, rotated frames, limits, and control/physics timestep checks. No spider dependency required. |
| P4 — ROS execution transport | Versioned envelopes and independent simulator/inference processes | P3 | Real ROS exchanges on both supported stacks cover request matching, duplicates, gaps, timeout, process death, wrong session, QoS mismatch, and clean shutdown. Direct and ROS replay agree within fixed numerical tolerances. Measure serialization, copying, and process/transport overhead against the in-process path. |
| P5 — Real-model migration and export proof | Opt-in SmolVLA migration plus a frozen locomotion adapter/exporter if the spider work is available | P1–P4; selected model artifacts | Actual models validate the contract exercised by P1–P2 fixtures. Camera/task inputs and named joints work without locomotion assumptions. No scalar arrival-history indexing for migrated state. Frozen reference outputs and reset tests pass; retain legacy behavior until migrated presets are validated. |
| P6 — Action chunks | Bounded executor and explicit replacement/cancellation rules | P2, concrete P5 need | Fake-clock tests prove timing, horizon, expiry, task-change/reset flushing, and stale-result rejection without needing GPU inference. Integration uses one real chunk-producing adapter. |
| P7 — Isaac bridge | Engine-local adapter and shared ROS contract; isolated dependencies | P1–P4; pinned Isaac environment | Same reference policy source runs against the compatible Isaac fixture. Verify mapping/time/reset/process-loss behavior and record engine-specific simulation results. No cross-engine physics-equality claim. |
| P8 — Hardware bridge | Named state assembly, SI routing, IMU/estimator integration when needed, shared control lifecycle | P1–P4; selected driver, bus-runtime and lifecycle work | Replay/injected-failure checks first; then separately authorized bench evidence for actual sensor frames, conversions, freshness, command timing, ownership, expiry/stop, and recovery. |

P7 and hardware preparation can proceed independently after their prerequisites.
Do not block the first MuJoCo contract demonstration on IMU availability, a full
PPO run, or Isaac installation. Do not declare the hardware bridge complete
until the selected physical path has evidence. If the spider branch is not
merged, keep P3 runnable with its small fixture and review any selected port
separately instead of importing the entire training/CAD branch.

## 8. Validation and evidence

| Level | Evidence | What it establishes |
| --- | --- | --- |
| Contract/unit | Malformed inputs, incompatible bindings, controllable clocks, restart and lifecycle faults | Software semantics and failure handling |
| Recorded inference | Frozen inputs, outputs, transforms, bundle hashes, and explicit tolerances | Export/preprocessing equivalence for the captured cases |
| Interface overhead | Matched direct, shared in-process, and ROS runs with numeric and image fixtures | Incremental cost of validation, assembly, scheduling, copying, and transport on the tested workload |
| ROS integration | Actual process-separated exchanges, correlation, queue bounds, failure injection, latency distributions | Transport behavior on the tested host and ROS stack |
| Closed-loop simulation | Model/version/seed, duration, controller period, task metrics and failure counts | Task performance in that specific simulated environment |
| Hardware | Config/calibration/firmware identities, measured timing and motion, sensor validity, fault/recovery trials | Qualified behavior only for the recorded physical setup |
| Developer workflow | Timed second-policy integration and compatible-backend switch, with source/config diffs and assistance recorded | Whether reuse reduces user integration effort under the recorded starting conditions |

For implementation PRs, run focused Bazel tests inside the Docker development
environment, then the applicable Compose `test-u22` and `test-u24` tasks before
claiming CI coverage. Build/test core helpers without GPU or simulator SDKs;
mark model-weight and Isaac execution checks as separate integration work and
record unavailable prerequisites instead of calling them passed. Format edited
BUILD files with buildifier and follow the repository lint workflow.

Record policy version, backend, execution mode, input sample identities and
ages, inference latency, command age, missed deadlines, rejected outputs, reset
events, and fault reasons. Separate startup from steady-state measurements.
Freeze task metrics, trial counts, workload, and numerical/latency thresholds
before qualification; no universal 50 Hz or walking-success threshold applies
to all policies. Hash large artifacts and store them through the established
artifact workflow instead of committing checkpoints or recordings here.

### Measure development effort

During P5, record the work to integrate another actual policy on an existing
robot/backend. During P7 or P8, record switching a compatible policy to the
additional backend. Count bridge implementation separately from subsequent
use of an already supported bridge. Where practical, have an engineer outside
the implementation team follow the documented workflow.

Measure elapsed and hands-on time to the first validated run, policy-adapter
edits, shared-runtime edits, configuration and artifact changes, debugging
iterations, and assistance required. Separate installation/downloads, training,
model preparation, calibration, and physical qualification from integration
time; retain them in the total workflow report. Record failed attempts and
missing prerequisites as well as successful runs.

Freeze starting conditions, supported capabilities, completion criteria, and
comparison tasks before measuring. Compare with an equivalent existing bespoke
integration where evidence is available; otherwise report absolute effort
without claiming a speedup. The reuse target is no policy-source edit for a
compatible backend switch and no model-specific shared-host edit for a policy
using existing capabilities. If either is needed, record why and revise the
boundary or supported scope before claiming the development benefit.

### Measure the cost of the abstraction

Benchmark the same deterministic policy and input sequence through direct
adapter calls, the shared in-process runtime, and (from P4) the ROS path.
Keep validation guarantees equivalent for the two supported execution paths;
bare adapter calls are a measurement baseline, not an alternative deployment
path that bypasses admission checks. Separate policy computation from framework
work, and serialization/copying from process and transport costs where measurable.

Cover small numeric state and representative camera resolutions/rates. Record
p50/p95/p99 and maximum latency, CPU use, allocations/copy counts or their
measurement limits, bytes copied, and peak/steady-state memory. Preserve host,
dependency versions, payload sizes, warmup, repetitions, and startup results.
Use owned immutable buffers when they avoid unnecessary image copies; lifetime
and mutation guarantees still apply.

Set acceptable added latency and memory budgets for each workload before
accepting P2/P4 performance results. If the shared path exceeds them, simplify
or optimize the responsible helper before broad migration. Keep noisy wall-time
benchmarks outside ordinary unit-test pass/fail gates; retain reproducible
reports as acceptance evidence. Do not let a slow model hide framework cost or
claim zero-copy behavior without measurement.

## 9. Decisions to resolve in implementation

- **Wire representation:** choose the smallest versioned envelope compatible
  with existing ROS/protobuf conventions; use standard sensor messages where
  their metadata is sufficient. Define how sample identity and command lifetime
  travel across the boundary before P4. Do not hide these in float indices.
- **Initial command sink:** position control is the first supported mode.
  Document whether the selected existing ROS path preserves all batch metadata
  or needs an admission wrapper. Require separate design for synchronized
  multi-board application or Cartesian/effort control.
- **Shared ownership integration:** place policy admission under the runtime
  control owner from the demo plan; reconcile with current MCU/standalone-node
  behavior before P8. The inference host must not acquire a second device owner.
- **Early policy fixtures:** define the numeric locomotion and image/task
  fixtures together in P1, exercise both throughout P2, and use their results
  to revise the minimum contract. P5 adds actual SmolVLA and locomotion model
  evidence. Pin downloadable models and record their runtime requirements.
- **IMU and estimation:** select the driver and estimator in their own work;
  freeze frames, gravity convention, timestamp provenance, and validity before
  enabling a locomotion policy on hardware.

The abstraction is ready to stabilize when a locomotion-style controller and
an image/task policy use it without model-specific branches in the shared host,
and a compatible policy uses two execution backends without policy-source edits.
Publish the supported combinations and their evidence separately from this
architectural target.
