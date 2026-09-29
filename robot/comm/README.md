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
| EtherCAT | no | JW2 paired only | JW2 paired only |

`ByteStream` provides ordered bytes without boundaries.
[`MessageTransport`](interfaces/message_transport.h) exposes only `Send` and
`Exchange` over borrowed byte spans; the adapter supplies response framing and
returns owned bytes. Factory-created message adapters are ready to use, so a
JW2 session reset does not reopen the physical link.

[`CorrelatedCyclicTransport`](interfaces/correlated_cyclic_transport.h) is a
separate interface with a finite exchange timeout and correlation/stop contract.
Its JW2 EtherCAT adapter is supplied together with a message endpoint when
`transport_type: MESSAGE_AND_CYCLIC` is selected.
The legacy TI-demo API has been retired. `CommTransport` can
represent either capability or `PairedTransports`, which holds both and the
configured response deadline. UDP remains rejected.

The [EtherCAT owner worker](ethercat/README.md#owner-worker-and-factory-assembly)
now provides background process-data exchange, blocking startup SDO and an
explicitly budgeted incremental runtime SDO path for unsegmented objects up to
76 bytes. Blocking SDO closes before
cyclic operation. The [paired JW2 adapter](ethercat/README.md#paired-joshuawire-v2-adapters)
adds the compatibility gate, reset-object handshake, CoE management envelopes,
PDO correlation and shared endpoint lifetime. Factory assembly validates all
discovered slaves before OP, then leases one endpoint per configured slave on a
shared NIC owner. The separate AM243 JW2 firmware profile implements this
contract; it is not the TI demo. See [config selection](../../config/README.md#joshuawire-v2-over-ethercat).

Device protocol parsing remains outside this layer. For example, the lidar
parser interprets bytes received through `ByteStream`, while a board codec
interprets complete exchanges received through `MessageTransport`.

### Transitional serial v2 exchange

`MessageTransport::Exchange` accepts a request and returns a variable-length
response. Fixed-size `SendAndReceive`, `Write` and legacy `Open` live separately
in [`LegacyMessageTransport`](interfaces/legacy_message_transport.h), used only
by remaining v1/vendor consumers. New adapters need not implement those methods.
Remove that compatibility seam after those consumers gain framed adapters.
Serial currently
implements `Exchange` with JoshuaWire's sync/length framing, a 64-byte cap, and
one 100 ms deadline spanning write and read. It flushes stale input before each
request and serializes exchanges on the bus mutex. CRC, version, and correlation
validation belong to the board's v2 session. A mismatched reply fails
the operation (outcome unknown), rather than waiting for another reply.

This framing implementation is transitional: the separate framed-serial adapter,
configurable serial deadlines/settle policy remain pending under the
board/comm separation plan. EtherCAT timing is explicit in comm config.

### Dependencies and tests

Concrete serial and SOEM transport targets are visible only within `robot/comm`.
Raw SOEM is restricted to the EtherCAT package. `CommFactory` keeps concrete
dependencies private with `implementation_deps`; public factory headers expose
capabilities, not concrete classes. All EtherCAT I/O/metadata headers are now
comm-internal; no raw bus alternative is exposed by the factory.

[`testing/fake_transports.h`](testing/fake_transports.h) supplies maintained,
test-only message/cyclic doubles with queued results. They do not simulate bus
timing, workers or correlation; those require adapter tests. The legacy fake
formerly under `serial/` now lives in
[`testing/fake_legacy_message_transport.h`](testing/fake_legacy_message_transport.h)
so board tests do not pull in the concrete serial implementation.
`transport_contract_test` checks the interfaces and fakes;
`transport_boundary_test` fails during Bazel analysis if implementation headers
leak through the public factory/capability dependencies. Both run in the normal
Compose test services, without hardware.
