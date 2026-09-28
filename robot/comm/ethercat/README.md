# EtherCAT Transport

`robot/comm/ethercat/` is the home for generic EtherCAT master-side transport
code. It should not contain AM243 actuator semantics; those belong in action
drivers that consume a transport abstraction.

The public C++ boundary starts at `ethercat_transport.h`.
`soem_ethercat_transport.*` is the SOEM backend landing pad. Bazel pins and
builds upstream SOEM through `@soem`. The transport now opens and closes the
SOEM master socket, discovers slaves, forces SOEM's split LRD/LWR path, maps PDO
regions, exposes cached slave metadata, transitions slaves to OPERATIONAL, and
exchanges process data. Board-facing smoke tests live in
`am243_demo_smoke.cc` and `robot/board/am243/am243_driver_smoke.cc`.
`ethercat_status.*` contains transport-independent PDO and working-count
validation helpers.

SOEM is dual-licensed under GPLv3 or a commercial license. Treat the pinned
dependency as a production-capable master library only after Joshua's license
choice, recovery behavior, diagnostics, and real-time scheduling policy are
settled.

## Responsibilities

- Discover and configure EtherCAT slaves.
- Transition slaves through INIT, PRE-OP, SAFE-OP, and OPERATIONAL.
- Own cyclic PDO exchange and working-count validation.
- Expose process-data access to higher-level drivers without leaking a specific
  master implementation into the action layer.

## AM243 Constraint

The current LP-AM243 TI EtherCAT simple demo works with split LRD/LWR process
data cycles and does not work with LRW cycles. A Joshua transport backend used
with this firmware must force split LRD/LWR. LRW should only be revisited after
the board firmware or EEPROM/ESI configuration changes.

## Owner-worker foundation (not factory-wired)

[`ethercat_master.h`](ethercat_master.h) / `.cc` add the comm-internal
`EthercatMaster` and its backend seam, `EthercatMasterIo`. The worker alone
initializes, configures, starts, exchanges, checks AL state and tears down the
backend. Callers supply complete output shadows and receive copied input
snapshots with a sequence number. This is not yet JW2 request correlation.
Before entering OP, the caller must supply a protocol-validated stop image for
every slave; that image is also sent best-effort before shutdown. Zero bytes
are not assumed safe for arbitrary firmware.

`SoemEthercatTransport` implements startup-only CoE SDO read/write and timed
process-data/state operations. **SDO access closes at cyclic startup**, including
already-queued requests. SOEM 2.0.0's `src/ec_coe.c` uses fixed internal send
timeouts and repeated receive waits; its timeout argument is not a total
deadline. We therefore do not schedule it in cyclic slack or claim that slow
mailbox transfers preserve watchdog timing. Runtime management/ESTOP mailbox
delivery requires a bounded/incremental implementation before JW2 EtherCAT can
be enabled.

Caller deadlines include queue wait. Expired queued operations are not sent;
dispatched timeouts report unknown outcome and fault the master, preventing
late replies from satisfying later calls. Stop rejects new work and wakes
waiters immediately, but joining the worker still waits for an in-progress
SOEM call. Startup/discovery and teardown are not hard-deadline operations.
During cycling, process data precedes the state check; working-count failure,
loss of OP or a missed cyclic deadline faults the master. Deadline detection
is not a hard-real-time guarantee, nor proof of safe motor outputs. Firmware
watchdogs and real-time scheduling remain necessary.

The current factory/TI demo still uses the synchronous path. Do not open a
second owner on the same NIC: shared master leases, protobuf timing policy,
JW2 PDO/CoE adapters, compatibility checks and firmware profile are pending.
The new worker has no runtime preset or hardware smoke entry point yet.
Regression cases and a test-local I/O backend live in the existing
`soem_ethercat_transport_test.cc`; no additional test utility is needed.

## Non-Goals

- AM243-specific actuator mapping.
- UART or serial flashing/debug tools.
- SOEM-specific types in public action-driver headers.
