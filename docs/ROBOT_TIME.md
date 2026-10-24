# Robot time

`//utils:robot_time` provides a ROS-independent C++ time service:

```cpp
#include "utils/robot_time.h"

const double now = RobotTime();
```

The header exposes `joshua::RobotTime` through a global using declaration, so
callers can use `RobotTime()` directly wherever the header is included.
Actuator, perception and host-side board interfaces already include it and
declare the Bazel dependency. Drivers using those interfaces need no additional
include or per-driver dependency. Every call reaches the same process-global
clock.

Exposing the accessor does not change existing `LOG()` timestamps. Logging
integration, including communication-driver logs, is separate migration work.
Firmware timestamps remain in the device's clock domain until that clock is
synchronized with robot time or the host explicitly converts those timestamps
into the robot-time domain. Reading `RobotTime()` on receipt gives a host receipt
time, not the original firmware acquisition time.

Each process owns one clock. Helpers, node callbacks and worker threads all
reach that object; no module context or clock pointer is needed. Separate
processes have separate objects. A common configuration and external clock
synchronization are required to make their readings comparable.

## Clock implementations

`RobotClock` has three concrete implementations, private to `robot_time.cc`:

```text
RobotClock
├── PtpClock        — owns a PHC descriptor and converts PHC time to UTC
├── UtcClock        — reads CLOCK_REALTIME
└── MonotonicClock  — reads CLOCK_MONOTONIC for local boot-relative time
```

`MakeRobotClock()` selects and probes one implementation before global
installation. If PTP is unavailable and fallback is allowed, it creates a
`UtcClock`. `SystemClockReader` is the shared system-call adapter for opening,
reading and closing clocks; tests replace it through the internal `ClockIo`
interface. The adapter does not select the source or own the global clock.
Drivers continue calling `RobotTime()` without depending on a concrete class.

## Initialization and ownership

The launcher loads and validates the config and installs the selected clock
before choosing the operation mode. The C++ `ros2_utils::RunNode<T>` loads and
validates the config and calls `SetGlobalRobotClock()` before constructing the node.
Clock setup failure returns exit code 1 before any node-owned drivers or
workers are created. The selected source and fallback reasons are logged.

`SetGlobalRobotClock(std::unique_ptr<RobotClock>)` and `CheckGlobalClock()` share
one `std::once_flag`. The first successful initialization wins. Null installation
is rejected without consuming that opportunity. A later installation returns
`FailedPrecondition`, including when a helper already triggered lazy initialization.
Explicit setup must therefore precede the first application `RobotTime()` call.

For callers outside the runner, `CheckGlobalClock()` lazily selects a default
clock. A failed creation throws `std::runtime_error`; a later call can retry.
There is no replacement or reset API. Tests of global installation run in fresh
processes, and tests of source selection use fake system calls.

Storage is private to `robot_time.cc` and intentionally lasts until process exit
to avoid destruction-order problems. The OS reclaims an installed PHC descriptor
at exit. Uninstalled clocks close their descriptors when destroyed. Custom
`RobotClock::Now()` implementations must support concurrent calls and return
`double` seconds.

If native plugins are added later, they must share the owning runtime library;
statically embedding independent copies in multiple shared objects can duplicate
the state. The clock implementation and timestamp-source migration cover C++
only. The existing Python trajectory reader follows the packet field name and
seconds format; the AI directory retains its existing timestamp behavior.
There is no Python RobotTime implementation or binding.

## Configuration and time domains

`general.robot_clock` is defined by
[`RobotClockConfig`](../config/proto/robot_clock.proto).

| Source | Behavior | Time domain |
|--------|----------|-------------|
| `PTP` (default) | Read the configured PHC; fall back to UTC at startup unless `require_ptp` is true | UTC after subtracting the configured PHC-to-UTC offset |
| `UTC` | Read `CLOCK_REALTIME` | System UTC |
| `MONOTONIC` | Read `CLOCK_MONOTONIC`; explicit selection only | Local boot-relative time, not comparable across hosts |

The host bootstrap (`sudo ./scripts/setup.sh`, or `--cpu`) installs a udev rule
granting the `ptp` group read access to `/dev/ptp*` and adds the invoking sudo
user to that group. Log out and back in after setup for local processes to
inherit the membership. This configures device permissions only.

The standard Docker Compose developer and launcher services run as root with
`privileged: true` and `/dev` mounted, so they already have PTP device access
on a host exposing the clock. No image rebuild is needed for these host
permissions. Custom unprivileged containers must expose the device, for example
with `--device=/dev/ptp0:/dev/ptp0:r`; non-root processes also need the host
`ptp` group's numeric GID as a supplementary group (`--group-add GID`). Host
group membership is not automatically inherited inside a container.

The default PTP path is `/dev/ptp0`. PTP requires an explicit
`ptp_utc_offset_seconds`: PHC seconds minus UTC seconds. Obtain this value from
the deployment's PTP service; do not copy a historical leap-second offset.
Explicit zero declares that the PHC already follows UTC. If the offset is
absent, default selection logs a warning and uses system UTC without opening
the PHC. Existing presets need no changes.

For system UTC explicitly:

```text
general {
  robot_clock { source: UTC }
}
```

Example for a PHC that the operator has configured to follow UTC:

```text
general {
  robot_clock {
    source: PTP
    ptp_device: "/dev/ptp0"
    ptp_utc_offset_seconds: 0
    require_ptp: true
  }
}
```

`require_ptp` rejects a missing offset or an unreadable PHC. It does **not** verify
synchronization lock. Joshua neither starts a PTP daemon nor adjusts the clock.
The host must maintain synchronization and the operator must maintain the
configured offset. Automatic offset discovery, leap-second updates and lock
monitoring are follow-up work. A readable PHC alone is not proof of synchronization.

Linux supports PHC reads through a device-derived POSIX clock ID; see the
[kernel PTP documentation](https://www.kernel.org/doc/html/latest/driver-api/ptp.html).
For the PHC/system time-scale distinction, see
[linuxptp's phc2sys documentation](https://www.linuxptp.org/documentation/phc2sys/).

## Reading and failures

UTC and normalized PTP readings use a fixed platform epoch: `kRobotStartTime`
is currently zero (the Unix epoch). The clock combines whole and fractional
seconds directly into a `double`:

```cpp
(static_cast<double>(ts.tv_sec) - utc_offset - kRobotStartTime) +
    static_cast<double>(ts.tv_nsec) / kNanosecondsPerSecond
```

It never subtracts a process-specific startup time. Changing the platform epoch
later would change the time contract for every consumer. All clock reads use
`RobotTime()` returning `double`, and custom clocks continue implementing the
original `virtual double Now() const` interface. Combining the parts preserves
fractional seconds within double precision; the final value does not retain
every nanosecond at Unix-epoch magnitudes.

Both `ActionPacket` and `PerceptionPacket` carry `double timestamp = 2` in
seconds, matching `RobotTime()`. Drivers store that value directly and publishers
keep it through forwarding. Only ROS message headers split the value into
integer seconds and nanoseconds.

Once selected, the clock source stays fixed. Runtime read errors throw
`std::runtime_error` rather than silently switching sources or returning a
fabricated timestamp. Consumers call `RobotTime()` directly and leave clock failures
to their exception boundary. `TryRobotTime()` remains available as an optional
`absl::StatusOr<double>` accessor with exception handling and finite-value checks.
UTC/PTP can jump when the host adjusts time. Deadline values use
`RobotTime() + timeout_seconds`. Serial exchange timeouts follow RobotTime only:
pausing the clock pauses the timeout, and clock adjustments change the remaining wait.

## Acquisition timestamps and ROS conversion

Drivers now record RobotTime once and publishers retain it:

| Producer | Meaning of the recorded timestamp |
|----------|-----------------------------------|
| OpenCV camera | Host capture completion, before image validation and copying |
| STS3215 position | Host feedback receipt; firmware clock conversion remains separate |
| LDS01 lidar | Host receipt of the scan-start marker, before the remaining scan read |
| ALSA audio | Estimated first-sample time, subtracting unread-frame age; partial chunks retain the initial time |
| Headerless C++ ROS commands | Host receipt/validation time; no source acquisition time is available |

These are host acquisition estimates, not hardware exposure timestamps or proof
of synchronization. Physical acquisition timestamps require device support.
Firmware `timestamp_us` metadata remains in its own domain.

`//ros2/utils:robot_time_conversion` provides `ToRosTimestamp(double)` and
`FromRosTimestamp(builtin_interfaces::msg::Time)`, which returns double seconds.
They convert the representation without reading a clock:

```cpp
auto stamp = ros2_utils::ToRosTimestamp(packet.timestamp());
if (!stamp.ok()) return stamp.status();
message.header.stamp = *stamp;
```

The conversion supports positive, zero and negative times, rounds fractional
seconds to the nearest nanosecond with carry, and normalizes the nanosecond
component to `[0, 1e9)`. Non-finite values and values outside ROS's signed 32-bit
seconds range are rejected. Precision follows the double representation.
Camera and lidar publishers and JointState
position feedback use the original packet time rather than stamping again.
Scalar feedback messages have no timestamp field.

Incoming JointState stamps are stored as double seconds, including zero and
negative values. Both packets changed field 2 from integer `timestamp_ns` to
double `timestamp`. This changes the protobuf wire type from varint to fixed64;
old integer-encoded timestamps are not wire compatible. Regenerate protobuf
code and update communicating producers and consumers together.

ROS conversion does not translate between UTC, boot-relative, firmware, or
simulation domains. Capture and newly generated timestamps follow
`general.robot_clock`; `use_sim_time` does not change RobotTime or select a
simulation clock. Incoming source stamps retain their original domain. A graph
must use compatible domains before comparing stamps. A simulation-backed
RobotTime and domain metadata/translation remain follow-up work; no `/clock`
integration is added here.

## Publisher timers

`//ros2/utils:robot_timer` provides `CreateRobotTimer(node, period, callback)`.
The period and deadlines are double RobotTime seconds. Position, camera, lidar,
audio and Xbox publishers use this helper. Camera and lidar timers only publish
their own configured stream.

A ROS wall timer wakes the executor at `min(period, 0.001)` seconds to check
`RobotTime()`. The first callback is due at `RobotTime() + period`; subsequent
checks invoke it only when its RobotTime deadline is reached. Executor delays
can postpone callbacks. Small delays preserve the original cadence; multiple
missed periods produce one callback and restart the cadence from the current
RobotTime, avoiding catch-up bursts.

A paused clock pauses callbacks. A backward jump waits for the existing deadline;
a forward jump can make a callback due immediately at the next executor wakeup.
Clock exceptions propagate. Cancelling the returned ROS timer stops the wakeups.
Audio recreates its timer when the negotiated capture interval changes.

## Integration checklist

- [x] Shared C++ accessor, install-once ownership and concurrent reads.
- [x] Configurable PTP/UTC/local monotonic sources and startup fallback.
- [x] Pure config validation and C++ runner initialization before construction.
- [x] Shared accessor exposure through actuator, perception and board interfaces.
- [x] Hardware-free unit tests for source failures, ownership and startup order.
- [x] Initialize launcher time before operation-mode selection.
- [x] Keep RobotTime, the virtual clock interface, and packet timestamps in
      double seconds; convert to integer components only at ROS boundaries.
- [x] Migrate C++ host driver timestamps and preserve them through C++ publishers.
- [x] Schedule C++ publisher callbacks using RobotTime deadlines.
- [ ] Integrate robot time into logging, including board and communication-driver
      logs; exposing the accessor alone does not change `LOG()` timestamps.
- [ ] Synchronize firmware clocks or convert firmware timestamps into the
      robot-time domain while preserving the original acquisition time.
- [ ] Define simulation-time behavior and incoming timestamp-domain handling.
- [ ] Discover PTP offsets automatically and monitor synchronization health.
- [ ] Validate a synchronized multi-process deployment on configured hardware.

Serial deadlines, cached feedback expiry, publisher schedules and audio retry/stall
detection use RobotTime seconds. OS waits and executor wakeups use platform timers;
logging retains its existing clock semantics. Utility entrypoints that only display
incoming stamps do not create new acquisition timestamps and need no clock installation.
