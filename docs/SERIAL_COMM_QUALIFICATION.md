# Mixed serial qualification and evidence

P1–P5 software is implemented. P6 provides a reproducible software evidence kit
and the procedure below; **physical qualification is pending**. Passing a PTY or
fake ADC test does not establish USB throughput, physical motion, analog accuracy
or stopping latency. This document is the evidence checklist for
[the serial plan](SERIAL_COMM_IMPROVEMENT_PLAN.md), not a declaration of hardware
readiness.

## Frozen candidate and workload

| Item | Candidate |
| --- | --- |
| MCU/image | Teensy 4.1, `teensy41-mixed-scalar`, native USB CDC; no automatic upload |
| Firmware dependencies | PlatformIO 6.1.18; Teensy platform 5.0.0; exact framework/toolchain packages and compiler recorded by the runner |
| Sensor | One scalar analog input on A0/pin 14, ADC1 channel 7, function/device 128; 10-bit native counts |
| Actuator | Channel 0, STEP 2 / DIR 3 / active-low enable 4; 4000 pulses/s maximum, 20 us pulse width; actual drive, motor, supply and wiring must be recorded |
| Stream | 100 Hz acquisition (10000 us), latest cache; 50 Hz ROS publication; 10000 complete-frame bytes/s telemetry reservation |
| Command workload | At most 10 native position commands/s for the initial bench; no velocity trajectory or automatic target refresh; bounded independent status queries |
| Host budgets | 200 ms ordinary total, 50 ms per attempt, two attempts maximum; 40 ms polling; 16 ordinary operations / 4096 bytes; separate stop slot |
| Firmware watchdogs | 1000000 us communication; 500000 us moving-target freshness; explicit disabled-state configuration |
| Heartbeat | Reserved GET_STATUS every 100 ms, independent of ordinary queue admission; never renews target freshness |
| Acquisition limits | 1000 us conversion timeout; supported periods 1000..1000000 us; 100000 us schema age bound |
| TX / telemetry | Safety reply before ordinary reply before coalescible latest sample; 20 ms adapter TX-stall fault |

These are **configured software limits and candidate bench goals**, not measured
physical guarantees. Native USB bandwidth does not follow from the nominal
115200 serial setting. Fix the exact stable device path in local copies of both
the [runtime preset](../config/config_preset/example/teensy_mixed_scalar.pbtxt)
and [disabled recovery preset](../config/config_preset/example/teensy_mixed_recover.pbtxt).
The checked-in placeholder path must not be assumed to identify a connected board.
Record the USB serial identity and flashed artifact hash before every bench run.
Startup checks protocol/catalog/schema/boot continuity; it does not authenticate
the physical MCU or prove wiring.

## Reproduce software evidence without hardware

From the repository root on the Docker host:

```bash
python3 scripts/qualify_serial.py --output /tmp/joshua-serial-evidence --runs 10
docker compose run --rm -T test-u22
docker compose run --rm -T test-u24
```

The new output directory contains `report.json`, a source-file hash manifest,
Docker image IDs, compiler/PlatformIO versions, package inventory, `.hex`/`.elf`
hashes and copies, command logs, and repeated mixed-test XML. The script runs only
fixed test targets and a firmware build; it has no upload, launcher or real-device
operation. Reports fail on command failure, missing mixed-case evidence or source
changes during the run. `--skip-firmware-build` leaves artifact evidence absent.
Use an unchanged checkout for a release record. Keep raw reports outside git.

The soak repeats the production mixed PTY/ROS binding, coordinator memory link,
serial owner, continuous framing, vendor/lidar serial exchanges and firmware
fault tests on Humble and Jazzy. It exercises the same protocol through serial
and an independent memory capability. `software_stop_ack_us` is a PTY scheduling
measurement and a regression check below 100 ms; it is not a physical stop metric.
Full Compose results remain the CI evidence, separately from the selected soak.

## Bench procedure — operator setup and authorization required

Follow [AGENTS.md hardware safety](../AGENTS.md#hardware-safety--read-this-first).
No firmware has been flashed and no physical preset has been run for this work.
Before execution, confirm the exact board/sensor and stable port, analog voltage
and common ground, motor/drive/power state, reviewed pin map, secured mechanism
and accessible motor-power cutoff. First run with motor power disconnected.
Flashing is a separate deliberate operation following the
[firmware guide](../firmware/teensy/41/README.md).

1. Freeze source, `.hex` hash, package/compiler inventory and reviewed local
   configuration. Verify A0's input stays within the Teensy input limits using
   the sensor and board documentation. Record the actual scalar sensor part,
   signal range, supply, reference voltage and measuring instrument. A0 counts
   are not calibrated volts or another physical quantity.
2. Build `//launcher:joshua_main`, `//tools/serial:record_scalar` and
   `//robot/board/joshua_wire:joshua_wire_smoke` in the selected Docker environment.
   Validate both local configurations with `//config:joshua_validate_config`.
   Run the recovery probe with `--dry_run` first (command below). This preflight
   opens no hardware. Do not use the launcher's operation mode as a safety check.
3. After setup confirmation and deliberate flashing, launch only the reviewed
   mixed runtime config through `joshua_main`. Confirm enable remains deasserted
   on an instrument. Record the scalar and health topics for 60 seconds. Vary
   the analog input through measured reference points; test disconnected/failing
   acquisition separately. Invalid samples must remain marked invalid/NaN and
   command/status service must continue. Constant input still has new acquisition
   sequence values; repeated cached values do not establish acquisition.
4. With motor power still disconnected, explicitly enable through
   `/mixed_stepper/torque` (BOOL). Send reviewed position commands at 10 Hz for
   ten minutes while recording samples, health, command timestamps and STEP/DIR/
   enable signals. Stop publishing commands for at least 500 ms during an
   unfinished move. A healthy heartbeat must not prevent target expiry. Separately
   kill the host process and verify communication-loss disable; record actual
   firmware loop and output timing. Restart must not clear the latch or enable.
5. Exercise the fault matrix below, returning to deliberate disabled recovery
   between cases. Test unplug/replug and reboot with the stable endpoint verified;
   old session/token/sample identities must never become usable again. There is
   no automatic port reconnection, subscription restart or target replay.
6. Only after the disconnected-power checks pass, obtain separate authorization
   for powered motion. Repeat with secured mechanics and a power cutoff. Measure
   motor stop independently from GPIO disable and protocol acknowledgment. Record
   drive inertia, load and braking/holding behavior. Software reports physical
   verification as unsupported throughout.

Inside the matching dev container (u22/3.10 flags shown):

```bash
bazel build --config=u22 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.10 \
  //launcher:joshua_main //tools/serial:record_scalar \
  //robot/board/joshua_wire:joshua_wire_smoke

# Read-only ROS recorder; the runtime is launched separately after authorization.
bazel-bin/tools/serial/record_scalar --topic /analog_a0/observation \
  --sensor analog_a0 --seconds 60 --output /tmp/analog-a0.jsonl

# Disabled recovery preflight. Stop the runtime before a real recovery.
bazel-bin/robot/board/joshua_wire/joshua_wire_smoke \
  --config=/path/to/reviewed-recovery.pbtxt --board=mixed_mcu \
  --mode=configure --channel=0 --allow_recover --dry_run
```

The recorder subscribes only; it never opens a board or sends commands. It retains
the complete scalar fields and health messages, bounds records to 64 MiB, and
reports missing/malformed observations. Duplicate publication is permitted by the
latest-value model; use boot/session/token/acquisition sequence to count distinct
acquisitions. Recorder and source receipt timestamps are separate clock-domain
facts; do not subtract timestamps from different machines as if synchronized.
For a complete capture, also use ROS bag on observation, health, commands and
`/rosout` topics. A successful recorder exit is receipt evidence, not qualification.

## Hardware topology comparison (TODO)

TODO: compare a shared actuator/sensor board and link with separate actuator and
sensor-acquisition boards, each on its own physical link, following
[Hardware topology and latency budgets](ARCHITECTURE.md#hardware-topology-and-latency-budgets).
This comparison is outstanding; existing mixed-operation checks do not establish
which topology meets a particular workload's timing requirements. Use the bench
setup and authorization procedure above for physical execution.

- Keep actuator commands, independent sensor payloads/rates, firmware behavior,
  host load and measurement methods comparable; record unavoidable differences.
  Keep encoder/current feedback required by the local motor-control loop on the
  actuator controller in both arrangements.
- Record board and firmware identities, physical links, USB hub/controller sharing,
  configured budgets, and the required timing/bandwidth margin before testing.
  Define command-application and stop limits and acceptable sensor loss/age before
  collecting results.
- Compare nominal load, sensor saturation/bursts, slow consumers, and host/USB
  contention. Record achieved acquisition/delivery rates, loss/coalescing counts,
  queue occupancy, rejected operations and deadline misses for each topology.
- Measure host command submission to controller application, stop request to
  output disable, and stop request to physical motor stopping. Record protocol
  acknowledgment latency separately. Use a common measurement clock or document
  clock alignment and uncertainty; do not subtract unsynchronized timestamps.
- Report p50/p95/p99, observed maxima, run duration and sample counts alongside
  every deadline miss. Compare results with the declared limits and margin;
  separate links and an observed maximum alone do not prove a hard real-time
  bound. Retain raw evidence and unresolved limits for both arrangements.

## Deliberate recovery

After stop/watchdog/fault, secure the mechanism and remove motor power. Stop the
runtime and close all port owners. Inspect the failure and verify the stable
device identity. After explicit setup confirmation, replace `--dry_run` above
with `--confirm_hardware`; **keep `mode=configure`, never add `--allow_enable` or
an exercise target**. The probe starts a disabling session, explicitly clears the
stop latch, configures the reviewed GPIO without enabling, reads feedback and
cleans up with another disabling reset. This path is tested against the actual
mixed firmware over a PTY. It does not restore sensor subscription or watchdog
configuration. Recovering does not prove that the underlying fault is fixed.

Restart the mixed runtime only as a deliberate new generation. It rechecks
identity/catalog/status, configures watchdogs and channels, and creates a fresh
subscription. It stays disabled until a new explicit enable command. Old targets
and handles stay retired. If the probe or restart fails, retain the logs and
leave power disabled; do not infer non-execution from a timeout.

## Fault matrix and release gates

| Case | Software evidence | Physical evidence still required |
| --- | --- | --- |
| Mixed command + 100 Hz acquisition + 50 Hz publisher | Production PTY/ROS test; one session/owner, disabled startup, typed metadata | Ten-minute workload: command completion p50/p95/p99/max, distinct acquisition rates/gaps, queue/loss counts, CPU and memory |
| Slow/invalid ADC | Actual mixed service loop + ADC register fixture; invalid samples and continuing control | Reference-point accuracy, signal disconnection/failure, conversion and loop timing |
| Slow consumer / overload | Bounded/coalescing stream caches, explicit loss, control/stop reservation tests | Slow publisher/recorder and USB saturation; telemetry may drop, control must remain within declared budgets |
| Lost replies / late bytes / duplicate IDs | Exact retries, unknown outcomes, no target replay, reserved stop tests | Cable/USB interruptions under command load; observed outcome and deliberate recovery |
| Blocked TX / reply admission full | Actual firmware loop/serial binding: retained replies, bounded TX fault and disabled outputs | Instrument output disable vs USB backpressure; no interleaved/partial frame corruption |
| Target freshness / silent host | Independent firmware watchdogs; heartbeat maintains holding output but never refreshes a moving target | Actual expiry and disable timing measured from last fresh request/target |
| Stop / reboot / reconnect / teardown | Reserved stop, fresh disabled generation, invalid old handles, operator recovery probe | End-to-end acknowledgment, GPIO disable and motor stop measured separately; inspect power behavior |
| Feetech / LDS01 compatibility | Full serial exchange and parser regression tests | Actual servo/lidar workload and long-run timeout behavior on their separate ports |

Candidate pass criteria: no unexpected enable, no stale-generation acceptance or
uncertain target replay; normal command completion within the configured 200 ms
total, status within 40 ms, and stop acknowledgment below the initial 100 ms goal
under the frozen workload. Keep every missed deadline and loss count; averages
cannot excuse violations. Output watchdog disable should occur by its configured
deadline plus the **measured** worst firmware-loop/service interval. Physical
motor stopping requires a load-specific limit agreed before powered testing; it
is currently unspecified and cannot pass. Changing rates, firmware, pins, drive
or budgets requires a new evidence record.

| Support claim | State |
| --- | --- |
| Linux x86-64 host, Humble / Jazzy | Software tests; no GPU needed |
| Teensy 4.1 mixed scalar candidate | Cross-build and native production-path tests; physical qualification pending |
| ESP32 serial request/response | Regression tests; no production mixed sensor binding claimed |
| AM243 / EtherCAT | Existing regression coverage; no mixed production sensor artifact claimed |
| Physical scalar sensor / motor / USB workload | Not qualified by this branch |
| Hard real-time deadlines, safety certification, physical stop verification | Not provided |

Release readiness requires a dated bench record with operator, exact sensor/drive/
motor, wiring/power, source/config/artifact hashes, instruments, raw captures,
commands, distributions/maxima, fault/recovery outcomes and unresolved failures.
Until those records exist, the hardware H/F/T gates and P6 physical gate stay open.
