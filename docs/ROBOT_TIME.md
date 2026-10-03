# Robot time

`//utils:robot_time` provides a ROS-independent C++ time service:

```cpp
#include "utils/robot_time.h"

const double now = joshua::RobotTime();
```

Actuator and perception drivers already include their shared interface headers.
Those headers expose this utility and import `RobotTime` into `robot::action`
and `robot::perception`, respectively. Driver implementations in those namespaces
can call `RobotTime()` directly without another include or a per-driver Bazel
dependency. The dependency is declared on the shared interface targets. The
interfaces add no clock member, wrapper method, or initialization; both names
refer to the same global function. Other helpers can include the utility directly.

Each process owns one clock. Helpers, node callbacks and worker threads all
reach that object; no module context or clock pointer is needed. Separate
processes have separate objects. A common configuration and external clock
synchronization are required to make their readings comparable.

## Initialization and ownership

The C++ `ros2_utils::RunNode<T>` loads and validates the config, creates the
selected clock, and calls `SetGlobalRobotClock()` before constructing the node.
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
`RobotClock::Now()` implementations must support concurrent calls.

If native plugins are added later, they must share the owning runtime library;
statically embedding independent copies in multiple shared objects can duplicate
the state. This change covers the current C++ executables, with no Python binding.

## Configuration and time domains

`general.robot_clock` is defined by
[`RobotClockConfig`](../config/proto/robot_clock.proto).

| Source | Behavior | Time domain |
|--------|----------|-------------|
| `PTP` (default) | Read the configured PHC; fall back to UTC at startup unless `require_ptp` is true | UTC after subtracting the configured PHC-to-UTC offset |
| `UTC` | Read `CLOCK_REALTIME` | System UTC |
| `MONOTONIC` | Read `CLOCK_MONOTONIC`; explicit selection only | Local boot-relative time, not comparable across hosts |

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
is currently zero (the Unix epoch). The conversion is:

```cpp
(static_cast<double>(ts.tv_sec) - utc_offset - kRobotStartTime) +
    static_cast<double>(ts.tv_nsec) / kNanosecondsPerSecond
```

It never subtracts a process-specific startup time. Changing the platform epoch
later would change the time contract for every consumer. A `double` does not
retain every nanosecond at Unix-epoch magnitudes; an exact integer accessor is
planned before packet serialization migration.

Once selected, the clock source stays fixed. Runtime read errors throw
`std::runtime_error` rather than silently switching sources or returning a
fabricated timestamp. Callers that later adopt this API must handle errors at
their worker/callback boundary. UTC/PTP can jump when the host adjusts time;
continue using monotonic durations for deadlines, watchdogs and sleeps.

## Integration checklist

- [x] Shared C++ accessor, install-once ownership and concurrent reads.
- [x] Configurable PTP/UTC/local monotonic sources and startup fallback.
- [x] Pure config validation and C++ runner initialization before construction.
- [x] Hardware-free unit tests for source failures, ownership and startup order.
- [ ] Initialize launcher time before operation-mode selection and cover other
      entrypoints that do not use the C++ node runner.
- [ ] Add exact integer timestamp access and explicit ROS timestamp conversion.
- [ ] Migrate driver acquisition timestamps and preserve them through publishers.
- [ ] Define simulation-time behavior and incoming timestamp-domain handling.
- [ ] Discover PTP offsets automatically and monitor synchronization health.
- [ ] Validate a synchronized multi-process deployment on configured hardware.

Existing timestamp producers and ROS clocks are unchanged in this first PR.
