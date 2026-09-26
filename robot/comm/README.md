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
provides atomic writes and request/response exchanges. EtherCAT currently
provides its cyclic transport directly. UDP remains rejected until its concrete
implementation is added.

Device protocol parsing remains outside this layer. For example, the lidar
parser interprets bytes received through `ByteStream`, while a board codec
interprets complete exchanges received through `MessageTransport`.

### Transitional serial v2 exchange

`MessageTransport::Exchange` accepts a request and returns a variable-length
response; legacy fixed-size `SendAndReceive` remains supported. Serial currently
implements `Exchange` with JoshuaWire's sync/length framing, a 64-byte cap, and
one 100 ms deadline spanning write and read. It flushes stale input before each
request and serializes exchanges on the bus mutex. CRC, version, and correlation
validation belong to the board's v2 session wrapper. A mismatched reply fails
the operation (outcome unknown), rather than waiting for another reply.

This framing implementation is transitional: the separate framed-serial adapter,
configurable deadlines/settle policy, cyclic v2 capability, and EtherCAT mailbox
are still pending under the board/comm separation plan.
