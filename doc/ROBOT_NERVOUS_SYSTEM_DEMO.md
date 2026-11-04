# Robot nervous system: hardware integration demo

| Field | Value |
| --- | --- |
| Status | Agreed demo direction; implementation and physical qualification gates open. |
| Created / last reviewed | 2026-11-03 |
| Implementation review | Checkout `07374c6`; source review only. No hardware qualification established by this document. |
| Primary outcome | Configure diverse supported hardware quickly, observe and control it dependably, and connect the same interfaces to a simulator. |
| Delivery owner, parts, capacity, and dates | TBD at N0. |
| Planning horizon | Target twelve weeks from an agreed kickoff; confirm conference and freeze dates at N0. |

This plan defines a hardware-integration demonstration and its acceptance evidence.
The separate [real-to-sim-to-real plan](REAL_TO_SIM_TO_REAL_DEMO.md) covers calibrated
modeling, policy training, and physical transfer in greater depth. Each plan has
its own gates; evidence closes only the conditions it actually demonstrates.

## 1. Product promise

> Build your robot's nervous system quickly. Connect supported actuators and
> sensors, see what they are doing, and keep applications working as hardware
> changes.

Joshua's contribution is the path from hardware configuration to useful, reliable
sensing and actuation. Demonstrate that path with working devices, visible config
changes, and measured engineering effort.

The release should establish four outcomes:

- **Fast integration:** a supported component becomes usable through configuration
  and documented calibration, with the required preparation and assistance timed.
- **Hardware diversity:** distinct actuator interfaces and sensor data types work
  together under one configured runtime graph.
- **Reliable operation:** data freshness, command handling, faults, and recovery
  have defined behavior and repeatable evidence under mixed load.
- **Simulation connectivity:** the same named hardware interfaces feed a simulator
  view, and one application can run through hardware or simulation adapters.

Count different drivers, protocols, and data types separately from device count.
Several identical servos demonstrate scale within one interface family. A second
qualified actuator interface demonstrates a different integration capability.

## 2. Required demo scope

Use a compact bench rig with independently mounted components. A complete robot
mechanism is unnecessary for demonstrating the integration interfaces.

| Component class | Proposed path | Required demonstration |
| --- | --- | --- |
| Feedback actuator | STS3215-compatible servo through a compatible Feetech bus adapter | Bounded command, measured position, direction/zero/limits, and command/feedback operation through one bus owner. |
| Second actuator interface | NEMA17-compatible stepper and rated external driver, through one selected Teensy 4.1 or ESP32 JoshuaWire board | Qualified physical motion, bounded commands, channel identity, and explicit feedback limitations. Select one board for the required rig. |
| Image sensor | USB camera through OpenCV | Live frames, device identity, acquisition/receipt timing where available, stream rate, and stale status. |
| Position sensor | Integrated feedback from the selected servo | Named measured angle with defined units and freshness; distinguish it from commanded position. |
| Range sensor | LDS01 lidar candidate through its serial driver | Live range visualization, frame/orientation, timing, and invalid/missing-data behavior. |
| Simulator | One selected Isaac Sim or MuJoCo integration | Every selected device has a scene representation or linked data panel with identity, source, units, and freshness. |

Exact models, inventory, electrical requirements, physical fixtures, and permitted
motion remain TBD. Select parts already available where possible. Qualify all
combinations before inclusion in the conference rig.

Stepper pulse counts or commanded positions are not measured shaft position.
Without independent physical feedback, label the stepper display as commanded or
estimated. A moving simulator model does not verify physical stepper motion.

The planned hardware kit also needs compatible adapters, correctly rated supplies,
accessible actuator power disconnects, protected moving parts, fixtures, cable
strain relief, a supported Ubuntu host, and a display. Record model/revision,
manufacturer instructions, wiring, power requirements, owner, price, and lead time
in the N0 BOM. Pin the firmware artifact for each Joshua-owned controller.

### Supported component changes

Require both of these changes:

1. Replace camera A with a qualified camera B. Update device settings and any
   required calibration; keep the shared display, recorder, and application code.
2. Add or remove the prequalified second actuator branch through configuration.
   Preserve existing device names and shared consumer code. Launch the expanded
   graph, verify both branches, and show the config diff.

Use a deliberate **stop → change → configure → validate → restart → inspect**
sequence. Include restart and calibration time in the measurement. New hardware
drivers and component-specific adapters are separate engineering work and must
be reported separately from configuring an already supported device.

Cross-family actuator substitution is an optional extension. Reuse requires a
compatible action/feedback contract, units, limits, and physical qualification;
matching a message shape alone is insufficient.

### Optional breadth

Add a second JoshuaWire board, audio telemetry, or a new sensor adapter only
after the required mixed rig and reliability gates pass. Current source includes
an ALSA audio construction path in the
[perception factory](../robot/perception/factory/perception_factory.cc); its full
runtime, display, and recording path still needs qualification. A VL53L5CX ranging
adapter or DYNAMIXEL actuator adapter would be separate development milestones.

Automatic discovery, energized component swapping, arbitrary hardware support,
complete sensor-physics reproduction, and a trained real-transfer policy are
outside this demo's required scope.

## 3. Audience walkthrough

Target a six-to-eight-minute presentation after rehearsal. Carry out long
qualification and endurance runs beforehand and show their recorded results.

| Step | Audience sees | Evidence |
| --- | --- | --- |
| Inspect the setup | Physical components beside the config and named device graph | Exact supported parts and the config that selects them. |
| Launch | Live camera, measured servo angle, range display, and device status | Measured launch-to-valid-data time and clear readiness conditions. |
| Command | Bounded servo and stepper motions with separate command/feedback views | Actual physical operation and truthful feedback labels. |
| Change hardware | Stopped camera replacement or actuator-branch addition; config diff | Measured change-to-usable time, including validation and restart. |
| Exercise a fault | A prescribed sensor interruption becomes visibly stale, followed by deliberate recovery | Detection time, application response, recovery time, and event log. |
| Connect simulation | Model motion and linked sensor views track the configured hardware | Named mappings, units, timestamps, data origin, and bridge delay. |
| Show application reuse | The same small application runs through the selected hardware or simulation adapter | Application source identity, adapter/config differences, and recorded outcomes. |

Choose the camera swap or branch addition for the live presentation; publish
evidence for both. Rehearse any physical fault demonstration and provide a labeled
recording for conditions unsuitable for the conference setting.

## 4. Shared interfaces and simulator behavior

The protobuf configuration remains the source of truth for device identities,
boards, channels, transports, limits, and artifact references. Generated models,
display mappings, and adapter metadata must reference that configuration snapshot.

```mermaid
flowchart LR
    Config[Protobuf configuration] --> Runtime[Joshua runtime graph]
    Devices[Actuators and sensors] <-->|Commands and observations| Runtime
    Runtime --> Monitor[Device health and live data]
    Runtime --> Record[Timestamped session record]
    Runtime --> Bridge[Simulator bridge]
    Bridge --> View[Scene and linked device panels]
    App[Shared application] --> Select[Explicit execution target]
    Select --> Hardware[Hardware adapter]
    Hardware --> Runtime
    Select --> Simulation[Simulation adapter]
    Simulation --> Model[Simulated plant and observations]
```

Hardware execution, live mirroring, recorded replay, and simulated execution must
have explicit labels. Route application commands to one selected execution target
at a time; define control ownership and deliberate enable behavior.

### What appears in the simulator

| Data | Live hardware view | Simulated execution |
| --- | --- | --- |
| Feedback joint | Display measured position with its age; show command separately. | Display simulated state from the selected plant model. |
| Open-loop actuator | Display commanded/estimated position with that label. | Display simulated state without presenting it as physical feedback. |
| Camera | Show real frames in a linked panel and the calibrated camera pose when available. | Render synthetic frames only if implemented and qualified for the application. |
| Range sensor | Display acquired points/ranges with their frame and validity. | Generate synthetic ranges only for the selected implemented model. |
| Device health | Show actual stream freshness, connection/error state, and recovery events. | Show simulator/adapter health with a distinct source label. |

Every demo device should be observable through this view. Not every real sensor
needs a synthetic equivalent for the live-visualization gate. Mark synthetic
outputs as unavailable when absent, rather than silently substituting real data
into a supposedly standalone simulation.

Select one simulator at N0 using a bounded bridge feasibility check: time to
integrate measured state and sensor panels, dependency/setup effort, mixed-load
performance, and application-adapter support. Isaac Sim's ROS 2 ecosystem is a
candidate integration route; Joshua's MuJoCo backend offers an existing stepping
and control interface. Freeze versions and ship one qualified path.

### Executable foundation for learning workflows

Use a small conventional target-bearing controller as the application example.
Give it a fixed observation contract containing target bearing/validity, measured
or simulated joint angle, and timing information. Produce a bounded angular
command through an explicit adapter.

Run the same application source with the physical servo and with a minimal
simulated plant. In simulation, generate the selected observations independently
of live hardware and mark any simplified detector/servo assumptions. Record
preprocessing, units, action semantics, update rate, and adapter differences.
The stepper branch can use a separate prescribed motion sequence; it need not
share the servo controller's semantics.

This demonstrates reusable interfaces and executable simulator connectivity.
Predictive model validation, environment/reward design, policy training,
checkpoint compatibility, and physical transfer evaluation remain additional
work for a real-to-sim-to-real claim. Use the
[learning-workflow plan](REAL_TO_SIM_TO_REAL_DEMO.md) for those acceptance conditions.

## 5. Reliability and integration evidence

Define workload and numeric acceptance bounds at N0 before qualification.
Document the host, device rates, camera resolution, number of channels, recorder
settings, and simulator mode. Evaluate the combined workload as well as each
device independently.

| Measure | Required definition and evidence |
| --- | --- |
| Launch to usable data | Start at the documented launch action; finish when every required stream is valid and the actuator path is ready. Report download/setup separately. |
| Supported change time | Stop-to-validated-resumption time, with wiring, config, calibration, restart, and assistance separated. Preserve failed attempts. |
| Engineering effort | Source edits, config edits, generated/artifact changes, driver development, and operator assistance. |
| Latency and jitter | Record measurement endpoints, p50/p95/p99 and maximum delay, and interval variation under the frozen workload. Distinguish command receipt from observed physical response. |
| Stream completeness | Expected/received samples, gaps, invalid values, and rejected data using explicit counters or sequence rules. Avoid inferring loss from display frame rate. |
| Freshness and fault response | Prescribed interruption, detection deadline, visible stale/fault state, dependent application behavior, and deliberate recovery result. |
| Endurance and lifecycle | Frozen soak duration and repeated start/run/stop/change cycles, including crashes, resource growth, timeouts, interventions, and denominator. |
| Outside-user reproduction | An engineer outside the implementation team follows the guide; record starting state, timing, help, and incomplete steps. |

Record acquisition and receipt timestamps separately where available. Follow the
[robot-time contract](../docs/ROBOT_TIME.md): a shared time accessor does not
establish synchronization across devices or processes. Use measured clock
relationships for cross-domain latency; otherwise report only defensible local
intervals. Preserve time-domain identity in recordings and the bridge.

For a lost sensor, freeze the rule for invalidating observations and stopping
dependent application commands. For an actuator, qualify stop, communication loss,
power interruption, and recovery against the selected firmware and electrical
setup before promising a physical response. The current
[firmware guide](../firmware/README.md) records no communication-loss watchdog for
JoshuaWire serial; a host timeout cannot guarantee a stop after that link fails.
Close that gap or constrain the declared operating envelope before qualification.

Keep health monitoring independent of the simulator display. Simulator or display
failure must produce the documented behavior without losing the evidence record
or silently changing command ownership.

## 6. Implementation foundations and gaps

These are source-level foundations. A factory path or passing software test does
not establish a qualified physical configuration. Simulator integration work is
required only for the backend selected at N0; the other backend is optional.

| Area | Existing foundation | Work needed for this demo |
| --- | --- | --- |
| Config-driven composition | [Architecture](../docs/ARCHITECTURE.md), protobuf config, and generated ROS 2 nodes. | Freeze rig presets, stable identities, readiness criteria, and supported-change validation. |
| Actuation | [Action factory](../robot/action/factory/action_factory.h) selects STS3215 and NEMA17 stepper drivers; [board factory](../robot/board/factory/board_factory.cc) resolves supported board paths. | Qualify both physical branches, command/feedback ownership, units, limits, command arbitration, stop, and recovery. |
| Sensing | [Perception factory](../robot/perception/factory/perception_factory.cc) constructs camera, STS3215 position, LDS01, and ALSA paths. | Verify the selected sensor streams end to end, including timestamps, invalid data, freshness, recording, and display. |
| Operator view | [Control panel](../ui/README.md) provides a UI foundation. | Build and qualify the required live device/status views; a CLI plus simple panels is acceptable. |
| MuJoCo integration | [Engine](../simulation/mujoco/engine.py) steps/reads/renders; [mirror mode](../simulation/mujoco/modes/mirror.py) writes incoming scalars to actuator controls. | Required only if MuJoCo is selected: implement and verify named measured-state mappings and sensor panels. Control inputs alone do not establish exact measured-state mirroring. |
| Isaac integration | [Viewer](../simulation/isaac/viewer.py) loads a scene and steps physics. | Required only if Isaac Sim is selected: implement the hardware bridge, device panels, command-target selection, and application adapter. |
| Timing and records | [Robot-time service](../docs/ROBOT_TIME.md) and [data collection](../ai/train/README.md) are foundations. | Preserve time domains, sample identity, config provenance, events, and replayable evidence; verify mixed-load behavior. |
| Physical dependability | [Firmware records](../firmware/README.md) identify board-specific behavior and gaps. | Qualify the exact board/firmware/driver/transport combinations and declared fault responses. |

## 7. Checkpoint register

Weeks are planning targets from kickoff. Assign owners, actual dates, capacity,
numeric bounds, and the supported hardware matrix at N0. All gates start open.

| Gate | Target | Required result and evidence | Owner / status |
| --- | --- | --- | --- |
| N0 — Contract, parts, and simulator | Week 1 | BOM and inventory; config/observation/action contracts; one simulator feasibility result; workload and frozen acceptance bounds; conference/freeze dates. | TBD / Open |
| N1 — Qualified mixed hardware | Weeks 2–4 | Both actuator branches physically qualified; all selected sensor types observable together; one-owner bus operation; units, feedback labels, stop/recovery evidence. | TBD / Open |
| N2 — Fast supported changes | Weeks 4–6 | Camera A→B and actuator-branch addition/removal reproduced from documentation; config/artifact diffs and complete timing ledger. | TBD / Open |
| N3 — Simulator and application reuse | Weeks 5–7 | All demo devices visible in one selected simulator view; independent simulated application run; explicit mappings, data origin, and adapter/source identities. | TBD / Open |
| N4 — Reliability | Weeks 7–9 | Frozen mixed-load, soak, repeated lifecycle, sensor-loss, and qualified actuator-fault protocols pass; complete metrics, failures, and recovery records. | TBD / Open |
| N5 — Outside-user reproduction | Weeks 9–10 | At least one engineer outside the implementation team reproduces setup and both supported changes; timing and assistance recorded. | TBD / Open |
| N6 — Release and conference | Weeks 11–12 | Pinned artifacts, verified combinations, fresh-host guide, relevant Docker checks, offline rehearsal, spares, and labeled fallback recording. | TBD / Open |

Downstream gates use the frozen upstream artifacts. A changed driver, firmware,
device, calibration, rate, or mapping reopens the affected qualification. N3 may
use a minimal scene while N1/N2 progress, but final evidence must use the actual
qualified hardware and configuration.

## 8. Release package and scope control

Ship the qualified hardware matrix, wiring/assembly guide, presets, calibration
artifacts, firmware identities, selected simulator asset/mappings, shared example
application and adapters, recording/inspection tools, and evidence manifest.
Include a support-status distinction between source implementation, software
validation, and physical qualification for every advertised combination.

The operator guide must cover first data, bounded command, supported change,
stale/fault inspection, deliberate recovery, recording, simulation/replay modes,
and troubleshooting. Distribute measured setup and change times with their
starting conditions, including prepared hardware and cached dependencies.

If milestones slip, remove optional device families and presentation polish
first. Preserve two distinct actuator interfaces, three required sensor data
types, both supported changes, and the reliability evidence for the full claim.
If those conditions cannot be met, publish a narrower verified support matrix
and identify the incomplete gates. Freeze one simulator; a second backend is
an optional extension after qualification.

Follow [AGENTS.md](../AGENTS.md) for physical runs and firmware operations, and
[CONTRIBUTING.md](../CONTRIBUTING.md) for Docker-based implementation checks.
This plan does not authorize hardware execution or firmware flashing. Inspect
every preset's devices and operation mode before any authorized run.

At each review record:

```text
Date / gate / owner / status:
Code / config / firmware / simulator / application / adapter identities:
Physical hardware, wiring, calibration, host, and workload:
Procedure, starting conditions, acceptance bounds, and trial denominator:
Measured timing, completeness, fault response, and recovery:
Source edits / config edits / artifact changes / assistance:
Failures, interventions, limitations, and evidence location:
Decision / affected gates / next owner and due date:
```
