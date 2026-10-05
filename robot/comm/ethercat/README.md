# EtherCAT Transport

`robot/comm/ethercat/` is the home for generic EtherCAT master-side transport
code. It should not contain AM243 actuator semantics; those belong in action
drivers that consume a transport abstraction.

Boards consume only `MessageTransport` and `CorrelatedCyclicTransport` from
CommFactory. Internally, the layers are:

- `JoshuaWireEthercatTransport`: one JW CoE/PDO endpoint per slave.
- `EthercatMaster`: one bus owner/worker per NIC, shared by those endpoints.
- `SoemEthercatBackend` (`soem_ethercat_backend.{h,cc}`): synchronous SOEM I/O
  behind the internal `EthercatMasterIo` interface. Not an alternative to JW.
- `ethercat_types.h`: private discovery metadata and process-data snapshots;
  `ethercat_status.*`: generic region/working-count validation.

The former `SoemEthercatTransport` name and public `EthercatTransport` API
are retired, together with the TI-demo board/driver, echo codec, legacy host
smoke tools and preset. There are no compatibility aliases. Vendor firmware
build/flash assets remain for historical bring-up; runtime requires the
separate JW profile. Serial v1/v2 behavior is unchanged.

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

## Board-independent JW contract

Host EtherCAT communication is selected by protocol capabilities, not board
model, vendor/product ID or slave-stack implementation. Every conforming board
uses the same `JoshuaWireEthercatTransport`, owner and SOEM backend. Board
identity is checked separately by `JoshuaWireBoard`; it is not a comm branch.

Firmware reuses the [shared endpoint](../../../firmware/common/README.md#porting-jw-ethercat-to-another-board)
and supplies board/stack adapters and real drive callbacks. SOES versus Beckhoff
SSC is solely a firmware dependency choice. Regression tests exercise another
board identity through the unchanged factory/engine/comm path without hardware.

### Current process-data constraint

Layout-v1 currently requires split LRD/LWR and rejects LRW. This policy began
with LP-AM243 bring-up but applies to every board on this path. A new board must
support it; any future mode extension must be generic and capability/config
driven, not a board-model special case.

## Owner-worker and factory assembly

[`ethercat_master.h`](ethercat_master.h) / `.cc` add the comm-internal
`EthercatMaster` and its backend seam, `EthercatMasterIo`. The worker alone
initializes, configures, starts, exchanges, checks AL state and tears down the
backend. Callers supply complete output shadows and receive copied input
snapshots with a sequence number. JW request correlation belongs to the endpoint above it.
Before entering OP, the caller must supply a protocol-validated stop image for
every slave; that image is also sent best-effort before shutdown. Zero bytes
are not assumed safe for arbitrary firmware.

`SoemEthercatBackend` implements blocking startup CoE SDO read/write and timed
process-data/state operations. **Blocking SDO access closes at cyclic startup**;
already-queued requests must be resubmitted. SOEM 2.0.0's `src/ec_coe.c` uses
internal send timeouts and repeated receive waits, not a total deadline.

Runtime SDO is explicitly enabled with a positive `mailbox_step_budget` and
`scheduling_guard`. [`coe_sdo_transfer.h`](coe_sdo_transfer.h) / `.cc` implement
the production mailbox codec/state machine: expedited or unsegmented normal
objects of 1–76 bytes that fit the configured mailbox. Segmented transfers,
redundant ports and SOEM's separate mailbox handler are unsupported on this
path. Each step performs at most one configured-station register datagram,
with nonblocking send, a single receive deadline and no write retransmission.
The master advances its transmit mailbox counter independently of the slave's;
reply counters need not echo request counters. Retained mailboxes are drained
before sending, and CoE service, object address, lengths and aborts are checked.
Mailbox repeats are not requested. One outstanding SDO and fault-on-timeout
prevent reuse of a late reply; the paired adapter below separately checks JW
session/generation correlation.

Process data runs first. While mailbox work is pending, mailbox steps alternate
with AL-state checks, with at most one management step per cycle. Pending
transfers yield between cycles; a step only starts when its budget and guard
fit. Invalid timing budgets are rejected. Hardware-free tests exercise a
stalled mailbox while process-data cycles continue until the caller deadline.

Caller deadlines include queue wait. Expired queued operations are not sent;
dispatched timeouts report unknown outcome and fault the master, preventing
late replies from satisfying later calls. Stop rejects new work and wakes
waiters immediately, then cancels the transfer on the worker. Joining still
waits for any in-progress I/O; startup/discovery, blocking startup SDO and
teardown are not hard-deadline operations. Working-count failure, loss of OP,
runtime SDO failure or a missed deadline faults the entire master and attempts
the stop images before teardown. Linux scheduling/poll granularity can overrun
a step budget; overruns are detected, not made impossible. This is not a
hard-real-time guarantee or proof of safe motor outputs. Firmware watchdogs,
real-time scheduling and real EtherCAT validation remain necessary.

The factory selects this path only for `MESSAGE_AND_CYCLIC`; standalone
`ETHERCAT + CYCLIC` is rejected with a retirement/migration error. JW config carries the slave
index, optional exact PDO region assertion and explicit timing policy. All
users of one NIC must agree on that policy. The first acquisition discovers
and compatibility-gates **every** slave before OP; mixed vendor/JW buses are
rejected, even when an incompatible slave is not requested by a board.

The factory creates one owner and one endpoint per discovered slave. All begin
with stop images; board initialization then resets/configures its selected
slave, leaving it disabled. Subsequent boards lease existing endpoints without
restarting the bus. Duplicate claims are rejected, including an endpoint
previously released while other leases remain. The last lease stops/joins the
owner under the NIC cache lock before allowing a reopen. This prevents a new
socket racing an old owner's teardown. These guarantees are process-local;
config validation also requires NIC consumers to share one ROS node process.
Never run an unrelated master on the same NIC.

Use the [config example](../../../config/README.md#joshuawire-v2-over-ethercat)
for explicit timing/address selection. Register failures include address,
elapsed time, budget and working count. Native regression coverage lives in
`soem_ethercat_backend_test.cc`; real-bus qualification is incomplete.

## Known master-side NIC timing issue

The AM243 bench host's Realtek RTL8125 NIC (`enp5s0`, `r8169` driver,
kernel `7.0.0-34-generic`) intermittently delayed received replies beyond the
5 ms bench deadline during normal interrupt-driven operation. The leading
suspect is the **master PC's NIC/driver receive and interrupt path**. A specific
driver or hardware defect has not been proven, and slave-side interactions
have not been conclusively excluded.

With the same AM243 SOES slave and firmware, changing only host reception to
continuous NAPI polling completed two runs of 500 disabled-channel feedback
calls, including initialization and teardown. Restoring normal reception
reproduced the timeout. Disabling software interrupt coalescing did not fix it.
Polling consumed approximately one CPU core and was used only for diagnosis;
all NIC settings were restored and Joshua has no NIC-specific workaround.

SOES hardware qualification remains pending on a suitable NIC, including target
commands, watchdogs, link/OP-loss recovery and the over-one-hour retirement
check. See the [recorded evidence and limits](../../../docs/JOSHUA_WIRE_VALIDATION.md#receive-path-controls-and-decision-to-defer-qualification).

## Paired JoshuaWire adapters

[`joshua_wire_ethercat_transport.h`](joshua_wire_ethercat_transport.h) / `.cc`
implement both `MessageTransport` and `CorrelatedCyclicTransport` for one slave.
Open claims the slave once per master lifetime and reads the 36-byte `JWEC`
descriptor before any reset or command. It requires protocol v2, layout v1,
80/80-byte PDO regions, a 64-byte frame limit, and CoE/PDO capability bits.
Errors report the observed artifact/profile and request a separate build/flash;
the TI demo is not accepted as JW firmware. The factory gates every
slave before entering OP and supplies validated stop images for the whole bus.

The shared [wire constants](../../../firmware/common/joshua_wire_ethercat.h)
define little-endian layout offsets and CoE indices. The adapter writes the
8-byte reset object, verifies its readback, then supplies the corresponding
JW reset acknowledgment. Firmware must publish that readback only after its
safety/session reset completes. No normal command is allowed before this step.

Each endpoint has a small dispatcher thread and one bounded queue shared by
both planes. It performs no direct bus I/O: SDOs go through the master queue,
and PDO exchange consumes copied snapshots. Message IDs increase across both
planes; generations never wrap within a session. Management commands use CoE;
only SET_TARGET/GET_FEEDBACK use PDO. Send-only is unsupported. Response length,
reserved/status fields, zero padding, CRC, session, generation, message ID,
command and channel are checked before returning owned response bytes.

Timeout includes queue waiting. Unsent queued work expires without publication;
published PDO work is invalidated with generation zero and reports unknown
outcome. Late PDO replies are acknowledged/discarded while idle or waiting for
a later request. Malformed PDO responses and failed mailbox exchanges require
a new verified session; a dispatched SDO timeout can also fault the master.
ESTOP drops queued work, cancels a cyclic waiter and invalidates its output
before the management exchange. It cannot interrupt an already-dispatched SDO.

Stop wakes endpoint callers and invalidates only that slave's shadow before
joining its dispatcher. Endpoints keep shared ownership of the master; closing
one does not stop another, and the last owner triggers master teardown. Slave
claims are not recycled, preventing a new adapter from adopting retained state.
The endpoint mutex is used for short queue/shadow-publication operations only;
SDO and snapshot waits release it. The master never calls endpoint code, so no
reverse lock acquisition exists. This extra dispatcher is not a second SOEM
owner and does not provide a hard-real-time scheduling guarantee.

The separate [AM243 firmware profile](../../../firmware/am243/joshua_dual_transport/README.md#opt-in-jw-ethercat-profile)
now implements this contract, including software command-progress/target
watchdogs. An existing test runs these adapters against its actual portable
core. Management exchanges publish an active-session idle image before sending
commands, including when reset preceded cyclic startup; otherwise the initial
all-zero stop image would immediately disable a newly enabled channel.

The shared board engine now routes management to messages and target/feedback
to cyclic exchange, using one JW session/ID allocator. Existing tests cover
the full BoardFactory/CommFactory path against the firmware core, two-slave
sharing, failed startup, duplicate claims and retained-channel teardown.
Single-board bench checks also passed software targets/feedback and stale-target
fault/recovery. Physical-output safety, cross-transport arbitration and broader
real-bus timing/failure validation remain pending.

## Non-Goals

- AM243-specific actuator mapping.
- UART or serial flashing/debug tools.
- SOEM-specific types in public action-driver headers.
