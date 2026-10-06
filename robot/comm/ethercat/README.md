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

## Non-Goals

- AM243-specific actuator mapping.
- UART or serial flashing/debug tools.
- SOEM-specific types in public action-driver headers.

## Owner worker migration

`EthercatMaster` transfers backend ownership to one worker. That worker handles
discovery, state transitions, cyclic snapshots and queued startup SDO calls.
Callers supply explicit timing budgets and complete output images. Expired
queued work is not dispatched; dispatched timeouts fail the owner and discard
late success. Stop wakes waiters before joining the backend. This intermediate
stage leaves the TI-demo factory path separate; JW mailbox/PDO adapters follow.

## Incremental CoE

`CoeSdoTransfer` performs at most one register datagram per step and never
retries a published write. `SoemEthercatBackend` implements Begin/Step/Cancel;
`EthercatMaster` schedules these steps within the declared mailbox budget and
guard while cyclic exchange continues. Missing support or insufficient slack
is rejected. Mailbox counters, malformed responses, abort codes, cancellation
and deadline overruns are covered by register fakes. The legacy TI adapter
remains separate until factory integration.

## Paired JW endpoint

`JoshuaWireEthercatTransport` exposes management message and correlated cyclic
capabilities for one compatibility-gated slave. The adapter checks the exact
descriptor/PDO region before session reset, retains replies until exact
acknowledgement, and validates session/message/command/channel correlation on
both planes. Timeout, cancellation and late replies invalidate or quarantine
the session instead of reporting unproven command execution. All bus I/O still
belongs to the master worker. Factory assembly is introduced in the integration
stage; the native adapter suite uses copied register/PDO data.
