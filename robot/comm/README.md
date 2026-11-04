# Communication

`CommFactory` is the shared communication entry point for boards, sensors, and
other devices. Configuration selects two independent properties:

- `comm_type` selects the mechanism, such as serial, UDP, CAN, or EtherCAT.
- `transport_type` selects the behavior exposed to the consumer: byte stream,
  message, or cyclic communication.

```cpp
ABSL_ASSIGN_OR_RETURN(auto comm, CommFactory::CreateComm(config.comm()));
ABSL_ASSIGN_OR_RETURN(auto stream, GetCommTransport<ByteStream>(comm));
```

Consumers never switch on `comm_type` and never construct mechanism adapters.
`CommFactory` validates the requested combination, opens or reuses the concrete
link, and returns the configured capability in `CommTransport`.

Current combinations are (continuous frames are a host integration capability):

| Mechanism | Byte stream | Message | Cyclic | Continuous frames |
| --- | --- | --- | --- | --- |
| Serial | yes | yes | no | JW integration |
| UDP | no | planned | no | no |
| EtherCAT | no | JW paired only | JW paired only | no |

`ByteStream` provides ordered bytes without boundaries.
[`MessageTransport`](interfaces/message_transport.h) exposes only `Send` and
`Exchange` over borrowed byte spans; the adapter supplies response framing and
returns owned bytes. Factory-created message adapters are ready to use, so a
JW session reset does not reopen the physical link.

[`CorrelatedCyclicTransport`](interfaces/correlated_cyclic_transport.h) is a
separate interface with a finite exchange timeout and correlation/stop contract.
Its JW EtherCAT adapter is supplied together with a message endpoint when
`transport_type: MESSAGE_AND_CYCLIC` is selected.
The legacy TI-demo API has been retired. `CommTransport` can
represent either capability or `PairedTransports`, which holds both and the
configured response deadline. UDP remains rejected.

The [EtherCAT owner worker](ethercat/README.md#owner-worker-and-factory-assembly)
now provides background process-data exchange, blocking startup SDO and an
explicitly budgeted incremental runtime SDO path for unsegmented objects up to
76 bytes. Blocking SDO closes before
cyclic operation. The [paired JW adapter](ethercat/README.md#paired-joshuawire-adapters)
adds the compatibility gate, reset-object handshake, CoE management envelopes,
PDO correlation and shared endpoint lifetime. Factory assembly validates all
discovered slaves before OP, then leases one endpoint per configured slave on a
shared NIC owner. The separate AM243 JW firmware profile implements this
contract; it is not the TI demo. See [config selection](../../config/README.md#joshuawire-over-ethercat).

Device protocol parsing remains outside this layer. For example, the lidar
parser interprets bytes received through `ByteStream`, while a board codec
interprets complete exchanges received through `MessageTransport`.

### Serial link timing

Serial caching and JoshuaWire board ownership use a shared device identity.
Existing symlinks are resolved before opening; character devices are matched by
device number, so aliases share one serial handle, I/O lock and timing policy.
One JoshuaWire board name owns that device for the board cache lifetime.
Config preflight uses the same identity for ownership and serial-policy checks,
reading filesystem metadata without opening devices. Absent devices use normalized
absolute paths, allowing offline validation; unresolved aliases cannot establish
device equivalence until their targets exist.

`post_open_settle_ms` defaults to zero and applies once per physical open.
Consumers of a port must agree on baudrate, settle policy, and effective `io_policy`. JoshuaWire adapters
also agree on their exchange timeout; raw byte-stream and caller-sized consumers
share the I/O owner without selecting JoshuaWire timing. ESP32 examples explicitly
request a 2000 ms settle interval. See [config migration](../../config/README.md#serial-timing).

### Dependencies and tests

Pure serial and paired EtherCAT field checks live in
[`factory/comm_config_validation.h`](factory/comm_config_validation.h), shared
by config preflight and `CommFactory`. Config validation depends on these checks
without linking the factory or concrete transports.

Concrete serial and SOEM transport targets are visible only within `robot/comm`.
Raw SOEM is restricted to the EtherCAT package. `CommFactory` keeps concrete
dependencies private with `implementation_deps`; public factory headers expose
capabilities, not concrete classes. All EtherCAT I/O/metadata headers are now
comm-internal; no raw bus alternative is exposed by the factory.

[`testing/fake_transports.h`](testing/fake_transports.h) supplies maintained,
test-only message/cyclic doubles with queued results. They do not simulate bus
timing, workers or correlation; those require adapter tests. The vendor caller-sized fake lives in
[`testing/fake_caller_sized_message_transport.h`](testing/fake_caller_sized_message_transport.h)
so board tests do not pull in the concrete serial implementation.
`transport_contract_test` checks the interfaces and fakes;
`transport_boundary_test` fails during Bazel analysis if implementation headers
leak through the public factory/capability dependencies. Both run in the normal
Compose test services, without hardware.

Serial `MESSAGE` configurations must explicitly select `serial_config.message_adapter`:

| Adapter | Boundary rule | Consumer |
| --- | --- | --- |
| `CALLER_SIZED` | Caller supplies each response length | Vendor drivers (Feetech) |
| `JOSHUA_WIRE` | JoshuaWire sync byte and length prefix | `JoshuaWireBoard` |

`JoshuaWireSerialTransport` implements `MessageTransport` directly; it does not
expose fixed-length forwarding. `CALLER_SIZED` retains input flushing and uses the configured
vendor transaction/idle budgets. Only `JOSHUA_WIRE` accepts `exchange_timeout_ms` (100 ms when
omitted), covering queue wait, write and read. Missing/unknown adapters and
message settings on `BYTE_STREAM` are rejected before opening the port.

`Serial` provides synchronous raw `Write`/`Read` and protocol-neutral transactions
by submitting owned operations to one Linux `SerialIoOwner` per physical port.
The worker alone performs post-open descriptor I/O, input flush, and close.
Nonblocking reads/writes use poll plus an eventfd wake path. Waiting callers hold
no owner lock. One FIFO operation runs at a time; a transaction covers its entire
write/read, while separate raw calls cannot provide that atomicity. Framing
callbacks run only bounded comm parser work, must not reenter/close/destroy the
owner, and finish before the synchronous call returns. Application callbacks
must run elsewhere.

Admission bounds active/queued operations and reserved I/O bytes. Total monotonic
deadlines include queue wait; raw/vendor read idle deadlines are independent.
Queued expiry and cancellation wake promptly even while another operation waits
for input. Comm-internal operation handles support cancellation and retain
partial bytes and a typed publication result: `kNotSent` or
`kMayHaveReachedDevice`. A write means bytes accepted by the OS, not confirmed
physical delivery or execution. Cancellation/timeout cannot recall buffered
bytes. The existing capability interfaces still return statuses, so their error
code alone is not a safe retry decision. Protocol correlation and reconciliation
remain the session/coordinator's responsibility.

Canonical path and character-device identity unify aliases. A process claim,
advisory file lock, and Linux exclusive tty claim guard against competing owners;
these do not evict preexisting or privileged external readers. The factory holds
weak leases, and the last consumer closes/joins the worker. Close is terminal:
old handles reject new work; a fault completes all waiters as unavailable. A fresh
owner generation requires releasing the old owner; there is no automatic reopen
or command replay. `CommFactory::SnapshotSerialConnections()` returns read-only
port/generation, state, queue, byte/error, and latency counters without exposing a
descriptor or concrete transport. Operation latency includes queue time.

See [serial config policy](../../config/README.md#serial-timing) for defaults and
bounds. PTY tests cover cancellation/publication, queue expiry, backpressure,
disconnect, close/reopen, aliases, framing limits, and fragmented full lidar scans.
They do not establish physical UART/USB/stop timing.

Runtime serial timeout arguments are double seconds; the factory converts
`exchange_timeout_ms` once at the config boundary.
Exchange deadlines use `std::chrono::steady_clock` for elapsed bus-lock wait,
write, and read time. RobotTime adjustments, pauses, and read failures do not
change serial exchange timeouts.

Device command and payload parsing remains outside this layer. For example, the lidar
parser interprets bytes received through `ByteStream`, while a board codec
interprets complete exchanges received through `MessageTransport`. JoshuaWire
CRC, version and session correlation remain in the board/session layer.

### Continuous frame reception (P3a)

[`FrameStreamTransport`](interfaces/frame_stream_transport.h) is a separate
complete-frame capability for one protocol coordinator. `FRAME_STREAM` selects it
through `CommFactory`; it cannot be obtained as `MessageTransport` or
`ByteStream`. Existing presets and board/session paths remain request-only. The
shared MCU coordinator, subscription management, priority scheduler and deliberate
reconnect reconciliation remain subsequent P3 work.

[`JoshuaWireFrameStream`](serial/joshua_wire_frame_stream.cc) runs the existing
bounded decoder on the same physical I/O worker. It receives between writes,
without input flushing, and validates revision/length/CRC. Receive work is bounded
per poll iteration so incoming telemetry cannot indefinitely defer a writable
frame. There is no additional reader thread or worker per consumer. Framed mode
rejects raw reads, request/response reads and input flushes; factory checks prevent
mixing either receive mode on one physical port, including aliases.

`WriteFrame` accepts one valid host frame and an absolute deadline, retains the
configured write cap, and reports bytes accepted by the OS. It does not wait for
a reply or allocate protocol IDs. A partial-write failure retires the link before
another frame can be written. Complete-frame bytes already buffered by the OS
cannot be recalled. `Receive` waits until its deadline; `Interrupt` retains one
pending scheduling wake without discarding frames. Close is terminal and wakes
waiters. Exactly one coordinator owns receive consumption and terminal close.

Reply storage is separate from notification storage and has priority on receive.
Notification arrival coalesces the same session/token/function; a full queue
replaces its oldest whole notification. Receipt timestamps survive queueing and
coalescing. Reply overflow faults the link, invalidates queued frames and wakes
waiters. Bounded snapshots expose assembly/queue occupancy, received/malformed
frames, partial-frame expiry, telemetry loss, and reply overflows. An incomplete
frame's idle expiry is applied and counted on the next input byte.

This layer recognizes only the notification envelope kind; it does not validate
sensor payload/schema, live subscription, boot, sequence or freshness. Those
remain shared protocol responsibilities. No frame can satisfy a command until
that coordinator validates correlation. See [configuration and defaults](../../config/README.md#continuous-frame-capability-p3a).

The validated interfaces configure timeout policy in adapters rather than adding
a deadline argument to every message call. The factory owns concrete adapters;
consumers receive capability interfaces. This migration does not introduce the
plan's proposed `CommLease` or `required_transports` API. Tests enforce concrete
adapter visibility and provide hardware-free capability fakes.
