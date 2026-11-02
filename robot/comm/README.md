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

Current combinations are:

| Mechanism | Byte stream | Message | Cyclic |
| --- | --- | --- | --- |
| Serial | yes | yes | no |
| UDP | no | planned | no |
| EtherCAT | no | JW paired only | JW paired only |

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

`post_open_settle_ms` defaults to zero and applies once per physical open.
Consumers of a port must agree on baudrate and settle policy. JoshuaWire adapters
also agree on their exchange timeout; raw byte-stream and caller-sized consumers
share the bus lock without selecting JoshuaWire timing. ESP32 examples explicitly
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
expose fixed-length forwarding. `CALLER_SIZED` retains existing input flushing
and read timing. Only `JOSHUA_WIRE` accepts `exchange_timeout_ms` (100 ms when
omitted), covering bus-lock wait, write and read. Missing/unknown adapters and
message settings on `BYTE_STREAM` are rejected before opening the port.

`Serial` provides raw `Write`/`Read` and a protocol-neutral `ExchangeUntil`
transaction. A caller-supplied boundary callback consumes bytes under one bus
lock across write and read; separate `Write` and `Read` calls cannot provide
that atomicity. Adapters explicitly choose whether to preserve or discard
pending input before a transaction. The caller-sized vendor transaction
also holds the same lock throughout its write and read.

Device command and payload parsing remains outside this layer. For example, the lidar
parser interprets bytes received through `ByteStream`, while a board codec
interprets complete exchanges received through `MessageTransport`. JoshuaWire
CRC, version and session correlation remain in the board/session layer.

There must be one receive owner per port. A future shared continuous receiver
must dispatch both replies and unsolicited sensor samples; it must not compete
with synchronous reads on the same port. Request/reply input flushing discards
samples, so it is unsuitable for that receive mode. This change provides an
explicit preserve-input policy but does not implement continuous reception or
allow independent readers to share incoming bytes safely.

The validated interfaces configure timeout policy in adapters rather than adding
a deadline argument to every message call. The factory owns concrete adapters;
consumers receive capability interfaces. This migration does not introduce the
plan's proposed `CommLease` or `required_transports` API. Tests enforce concrete
adapter visibility and provide hardware-free capability fakes.
