# Communication

`CommFactory` is the shared communication entry point for boards, sensors, and
other devices. Configuration selects two independent properties:

- `comm_type` selects the mechanism, such as serial, UDP, CAN, or EtherCAT.
- `required_transports` lists every behavior the consumer uses from that
  mechanism: byte stream, message, cyclic, or correlated cyclic. One link may
  provide several at once. The single `transport_type` field is still accepted
  for consumers that use exactly one; a config sets one form or the other.

```cpp
ABSL_ASSIGN_OR_RETURN(auto lease, CommFactory::Acquire(config.comm()));
ABSL_ASSIGN_OR_RETURN(auto stream, lease.Require<ByteStream>());
```

Consumers never switch on `comm_type` and never construct mechanism adapters.
`CommFactory` validates that the mechanism provides every required transport,
opens or reuses the concrete link, and returns a `CommLease` exposing exactly
those capabilities. Concrete serial and SOEM targets are visible only inside
`robot/comm/`.

Current combinations are:

| Mechanism | Byte stream | Message | Cyclic | Correlated cyclic |
| --- | --- | --- | --- | --- |
| Serial | yes | yes | no | no |
| UDP | no | planned | no | no |
| EtherCAT | no | planned (CoE) | yes (TI demo) | planned (PDO) |

`ByteStream` provides ordered bytes without boundaries. `MessageTransport`
provides deadline-bounded sends and request/response exchanges of complete
messages. `CorrelatedCyclicTransport` provides request/response exchange
carried in correlated slots of a cyclic process image. The raw cyclic
capability exposes the process image directly and exists only for the retained
TI EtherCAT demo.

Serial has no message boundaries, so a consumer that requires `MESSAGE` over
serial passes a `MessageFramer` in `CommOptions`. The framer is protocol
knowledge — it reads a response's length from its header — while the serial
adapter owns the port, the deadline, and serialization.

Device protocol parsing remains outside this layer. For example, the lidar
parser interprets bytes received through `ByteStream`, while a board codec
interprets complete exchanges received through `MessageTransport`.

Test doubles for the capability interfaces live in `testing/`.
