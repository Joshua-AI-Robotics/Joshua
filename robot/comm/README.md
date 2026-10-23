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
| EtherCAT | no | no | yes |

`ByteStream` provides ordered bytes without boundaries. `MessageTransport`
provides complete-frame `Send` and `Exchange` calls. `CorrelatedCyclicTransport`
provides correlated exchanges with an explicit finite timeout.
`CallerSizedSerialTransport` implements the `CallerSizedMessageTransport`
interface for vendor protocols. EtherCAT currently
provides its cyclic transport directly. UDP remains rejected until its concrete
implementation is added.

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
