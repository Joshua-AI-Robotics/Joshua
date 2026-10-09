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
The temporary `LegacyMessageTransport` adapter preserves fixed-length serial
reads for existing consumers while the shared board engine is migrated. EtherCAT currently
provides its cyclic transport directly. UDP remains rejected until its concrete
implementation is added.

Device protocol parsing remains outside this layer. For example, the lidar
parser interprets bytes received through `ByteStream`, while a board codec
interprets complete exchanges received through `MessageTransport`.

The validated interfaces configure timeout policy in adapters rather than adding
a deadline argument to every message call. The factory owns concrete adapters;
consumers receive capability interfaces. This migration does not introduce the
plan's proposed `CommLease` or `required_transports` API. Tests enforce concrete
adapter visibility and provide hardware-free capability fakes.
