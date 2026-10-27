# Host GPIO and board profiles plan

Status: **proposal; documentation only**. No GPIO backend, profiles, protobuf
changes, or hardware support are delivered by this PR.

Companion to [BOARD_LAYER_RFC.md](BOARD_LAYER_RFC.md) and
[ARCHITECTURE.md](ARCHITECTURE.md). This plan adds local GPIO and a common pin
catalog under `robot/board/`; it does not replace the RFC's transport work.

## 1. Outcome and boundaries

A Raspberry Pi 5 or Jetson can run Joshua and control its own pins through the
board layer. ESP32 and Teensy use the same profile and pin-reference model, with
resolved pin identifiers sent to firmware. Robot presets remain the single
source of truth for wiring and operating settings; profiles supply reusable
hardware facts.

All currently supported actuator factories resolve a `BoardChannel`. A board
need not be an external PCB: `FeetechBusBoard` represents the bus to controllers
inside the servos, and `HOST_GPIO` represents the local host controller.

```text
ROS node -> device driver -> board/channel -> backend -> physical device
                                  |
                       profile + configured wiring
                                  |
                            pin resolution

Local host backend: Linux GPIO / PWM / supported pulse engine
MCU backend:        configured transport -> firmware -> GPIO / pulse engine
```

Host GPIO runs inside the consuming node process, alongside the launcher on the
same machine. It needs neither a serial connection nor `joshua_wire`.

Initial scope is digital input/output and verified profile resolution. Hardware
PWM and motor pulse generation follow as capability-specific work. SPI, I2C,
UART, CAN, and ADC may appear in pin metadata without being implemented as raw
GPIO operations. Use their appropriate kernel/device interfaces when available.

## 2. Existing gaps

- `BoardType::HOST_GPIO` exists, but `BoardFactory` rejects it as unimplemented.
- `Channel.host_gpio` is in the same `drive_config` oneof as `step_dir` and
  `pwm`. A channel cannot express both drive settings and host wiring there.
- `HostGpioConfig.pins` is an unnamed list, with no explicit numbering convention.
- MCU `StepDirConfig` fields contain firmware-specific integer pin identifiers.
- Board caches are process-local. Existing serial ownership checks do not
  establish ownership of host GPIO lines or multiplexed peripherals.
- `BoardChannel::SetTarget(mode, value)` carries one target. A combined position
  and speed limit needs an explicit command contract, independently of GPIO.

## 3. Proposed location and responsibilities

```text
robot/board/
  proto/
    board.proto
    board_profile.proto
  profiles/
    raspberry_pi/
    nvidia/
    espressif/
    pjrc/
    README.md
  pin/
    pin_resolver.h / .cc
  host_gpio/
    host_gpio_board.h / .cc
    linux_gpio_backend.h / .cc
  interfaces/
    board_interface.h
    board_channel.h
```

Profiles are protobuf text data, packaged as Bazel runtime resources. A registry
resolves exact IDs and versions without relying on the working directory or
fetching files from the internet. Documentation and inspection output are
generated from the same data. No separate top-level `hardware/` directory and no
Python hardware runtime are introduced.

`HostGpioBoard` names a local backend, not a Pi/Jetson-specific implementation.
If the board RFC's engine composition lands first, integrate this backend there
instead of creating a parallel class hierarchy. Profile identity must remain
independent of protocol, transport, and drive choice.

## 4. Hardware profile contract

A profile identifies an exact board/module, carrier, and hardware revision.
"Jetson Nano" and "ESP32" alone are not sufficient. Original Jetson Nano and
Jetson Orin Nano are separate targets, as are different ESP32 development boards.
Module facts can be reused by carrier profiles through explicit, version-pinned
references; reject cycles and conflicting mappings. A complete board profile
must resolve without guessing its carrier.

Use stable string IDs, not a new enum value for each product. Distinguish schema
version from immutable profile-data version. A correction produces a new data
version; presets pin the version they were reviewed against.

Each profile records:

| Data | Required meaning |
| --- | --- |
| Identity | Vendor, model, module/carrier, hardware revision, applicable variants |
| Connectors | Connector ID, orientation reference, physical pin positions |
| Pin kind | GPIO, power, ground, reset, reserved, or unconnected |
| Signals | Canonical identity and explicitly qualified aliases |
| Electrical facts | Documented voltage domain, tolerances, direction restrictions and relevant per-pin/group limits |
| Functions | Digital input/output, edge detection, PWM, ADC and peripheral alternatives |
| Resource constraints | Pinmux alternatives, shared peripheral instances, boot/debug/flash restrictions |
| Backend mapping | Linux controller/line identity or firmware pin identifier and numbering convention |
| Applicability | OS/device-tree or firmware mapping variants where needed |
| Provenance | Official source URL, document revision/date, verification status |

Unknown is distinct from false, zero, or supported. Include every pin of the
connectors claimed by the profile, including power and ground. Unverified facts
must be marked and must not authorize hardware operations. Describe exposed
board connectors, not an unsupported promise to catalog every internal SoC pad.

Do not assume GPIO controller numbering is stable. Prefer verified controller
identity plus line offset/name; resolve the actual device at runtime. Line names
and chip labels can also be ambiguous, so validate matches and fail on ambiguity.
An explicit device selector is allowed for custom deployments, with identity
checks and a visible portability limitation.

## 5. Wiring configuration

Add an exact profile reference to `Board`. Separate channel wiring from drive
configuration. For known drives, prefer typed signal fields (`step`, `direction`,
`enable`) over positional arrays or arbitrary unvalidated strings.

A `PinRef` selects one addressing form: connector position, qualified signal
alias, or explicit backend address for custom hardware. Numeric zero is valid
for some lines; use field presence rather than treating zero as unset. All forms
resolve to the same canonical resource for conflict detection.

Illustrative syntax only; field names and numbers are not yet implemented. The
symbolic pin labels below must be replaced by reviewed board-specific mappings:

```text
boards {
  name: "local_controller"
  board_type: HOST_GPIO
  profile { id: "<exact-board-and-carrier>" version: 1 }
  channels {
    index: 0
    drive: STEP_DIR
    step_dir { max_pulse_rate_hz: 4000 }
    wiring {
      step_dir {
        step      { connector: "<header>" pin: <step-position> }
        direction { connector: "<header>" pin: <direction-position> }
        enable    { connector: "<header>" pin: <enable-position> }
      }
    }
  }
}
```

The example illustrates configuration separation, not supported host stepper
motion. Local boards omit `comm` and firmware handshake configuration. Existing
MCU boards retain both. Initial values, inactive polarity, shutdown behavior,
and requested bias/drive mode belong to wiring/channel configuration; catalog
capabilities constrain them. Define each setting once to avoid competing enable
polarity or timing values across drive and wiring messages.

Retain legacy MCU numeric pin fields during migration. Reject mixed legacy and
profile wiring for the same channel; do not silently choose precedence. Preserve
existing presets until explicitly migrated. Allocate protobuf tags only during
implementation after checking concurrent schema changes; do not repurpose them.

## 6. Resolution, ownership, and lifecycle

Resolution has three stages:

1. **Offline validation:** load profiles, resolve aliases, check pin kinds,
   required functions, electrical compatibility where known, duplicate resources,
   mux conflicts, and node ownership. This stage opens no devices.
2. **Read-only discovery:** identify the running platform, resolve Linux resources,
   inspect exposed capabilities/ownership, and report mismatches. Device-tree and
   carrier differences require explicit variants; auto-detection never silently
   substitutes another profile. Availability here is advisory, not a reservation.
3. **Acquisition:** the backend requests resources exclusively, establishes initial
   output values, and rechecks failures. On partial failure, unwind acquired
   resources in reverse order and return a detailed status.

Initially one node process owns each configured host board. Within it, channels
can share the backend, but overlapping pin claims are invalid unless a documented
capability explicitly permits sharing. Canonical physical identity catches
conflicts even across distinct board names or aliases. Kernel acquisition also
protects against external processes and races. Do not reserve unrelated pins on
an entire GPIO chip.

Publish feedback for other processes instead of reopening owned lines. A future
GPIO service can offer cross-process access if required; it is not a prerequisite.

Normal teardown applies configured inactive values and releases requests.
Destructors provide nonthrowing fallback cleanup. Returned failures retain device,
channel, and signal context and are logged at the node boundary. A process crash
or released line cannot guarantee a desired electrical level: external pulls,
driver enable behavior, and platform setup must cover that case.

## 7. Backend and device contracts

Use a C++ backend over the Linux GPIO character-device API, with a pinned
libgpiod dependency shared by Ubuntu 22.04 and 24.04 builds. Confirm the minimum
kernel ABI for each supported Pi/Jetson software image; a container does not
upgrade its host kernel. Report unsupported kernels explicitly instead of silently
falling back to a different pin-numbering API.

Expose request/release, digital read/write, and edge events behind an injectable
interface. Keep Linux calls out of motor drivers and ROS callbacks. Pinmux and
boot overlays are platform setup, documented per profile; Joshua does not rewrite
them automatically. Metadata support does not imply runtime access permission.

Digital switches and relays need typed digital channel/action/perception contracts;
do not encode a Boolean as a joint position or fake motor feedback. Add these
contracts and factory cases alongside the first digital backend. Preserve the
existing motion-channel interface for motors until a deliberate extension lands.

Hardware PWM is a separate capability/backend with controller/channel resolution,
frequency constraints, and explicit lifecycle. GPIO output capability alone does
not imply PWM availability. STEP/DIR requires a defined pulse engine, including
pulse width, direction setup/hold, rate limits, cancellation, and multi-channel
scheduling. Ordinary ROS timers or userspace sleeps are not a deterministic motor
pulse contract. Reject unsupported motion instead of silently degrading timing.

Before adding position plus velocity support, define whether velocity means an
independent velocity target or a position-move speed limit. Use a structured
command preserving field presence and validate supported combinations before any
writes. Coordinate MCU support with the board RFC's protocol work; do not make
host digital GPIO depend on a `joshua_wire` redesign. Pulse-count feedback must
remain distinguishable from measured encoder feedback.

## 8. Catalog population and inspection

Initial families: Raspberry Pi 5, the user's exact Jetson Nano/Orin module and
carrier, the deployed ESP32 development board, and the deployed Teensy model.
Exact carriers/revisions remain inputs to profile authoring, not assumptions to
bake into this proposal. Additional boards use the same schema and resolver.

For each profile:

- Collect official connector diagrams, datasheets, schematics, and software pin
  mappings; attach sources to facts and applicability variants.
- Cover all claimed connector positions and resolve aliases consistently.
- Review boot-sensitive/reserved pins, mux conflicts, power/ground, voltage
  domains, and firmware numbering against those sources.
- Generate a pin table and mark the distinction between documented and physically
  verified mappings. Never advertise a family-wide profile as universally verified.

Proposed read-only tooling: `joshua hardware describe <profile>` for catalog data,
`inspect <profile>` for runtime comparison, and offline preset wiring validation.
Command names are provisional. Inspection must not claim lines, change direction,
write outputs, start PWM, send motor commands, or flash firmware.

## 9. Implementation sequence and acceptance

| Phase / separate PR | Deliverable | Acceptance |
| --- | --- | --- |
| 1. Profiles and resolver | Schema, packaged registry, qualified PinRef, offline validation | Alias equivalence, unknown metadata, conflicts and version failures are handled without hardware |
| 2. Initial catalog | Exact-board profiles and generated pin tables | Claimed connectors complete; source provenance and applicability reviewed; unresolved facts explicit |
| 3. Local digital GPIO | Backend, host board integration, digital action/perception contracts | Exclusive ownership, inputs/outputs/edges, startup rollback and shutdown have defined behavior |
| 4. MCU adoption | Translate profile pins to existing firmware identifiers | Legacy presets remain valid; mixed wiring rejected; no protocol change solely for pin aliases |
| 5. PWM and motion | Hardware-backed capabilities and structured motor commands | Backend timing and control semantics demonstrated before claiming motor support |
| 6. Deployment and inspection | Docker device mapping, ARM64 setup, inspection tools and examples | Exact supported images documented; minimal device access; inspection has no control side effects |

Deployment support needed for phase 3 ships with that phase; phase 6 completes
polished tooling and examples. Package only the required devices/permissions;
avoid blanket privileged containers or raw memory access as the default.

Implementation touchpoints include board protos/factories, config resource
ownership validation, digital action/perception contracts, Bazel resources and
dependencies, Docker images/device access, and MCU pin resolution. Generic ROS
subscription configuration does not gain board-specific GPIO fields.

## 10. Verification and remaining decisions

This planning PR changes documentation only. No builds, tests, GPIO access, or
firmware flashing are needed or performed by the agent.

For implementation PRs, prepare fake-backend tests covering resolution, aliases,
resource collisions, missing permissions/capabilities, partial acquisition,
initial/inactive levels, edge events, and error propagation. Add compatibility
coverage for legacy MCU presets and packaged profile lookup. The user runs these
and `docker compose run --rm test-u22` / `test-u24`; CI on generic hosts cannot
establish electrical or timing correctness.

Hardware acceptance is user-run on the exact board/carrier/software combination:
verify pin mapping and initial/shutdown levels, then inspect PWM/STEP timing with
suitable measurement equipment before enabling motor loads. Never automatically
run a real-device preset or flash firmware.

Before implementing board-specific profiles, establish:

- Exact Jetson module/carrier, ESP32 development board, Teensy model, and revisions.
- Supported host kernel/JetPack/device-tree variants and pinmux setup.
- The first digital devices and any PWM/stepper timing requirements.
- Profile review ownership and the selected hardware pulse-generation mechanism.

These do not block the common schema/resolver design. They do block claiming
verified support for a particular pin mapping or timed output capability.

## References

- [Linux GPIO character-device API](https://docs.kernel.org/userspace-api/gpio/chardev.html)
- [Kernel guidance on subsystem drivers using GPIO](https://docs.kernel.org/driver-api/gpio/drivers-on-gpio.html)
- [libgpiod core API](https://libgpiod.readthedocs.io/en/master/core_api.html)
- [NVIDIA Orin NX/Nano platform and pinmux setup (r36.2)](https://docs.nvidia.com/jetson/archives/r36.2/DeveloperGuide/HR/JetsonModuleAdaptationAndBringUp/JetsonOrinNxNanoSeries.html)

These references guide backend design. Actual profile entries require their own
board-specific primary sources; this document supplies no verified pin tables.
