# Communication

Follow [robot/AGENTS.md](../AGENTS.md), the repository instructions, and shared
coding guidelines. Read [README.md](README.md) before editing. This file owns
the local architecture contract and rules for `robot/comm/`.

## Architecture contract

CommFactory selects concrete mechanisms and assembles capability interfaces.
Consumers never switch on `comm_type` or construct mechanism adapters. Keep
concrete serial/EtherCAT targets internal to this directory, raw SOEM internal to
EtherCAT, and concrete dependencies private through `implementation_deps`.
Public comm/factory compilation contexts must not expose implementation headers.

Transports own byte movement, framing needed by a selected adapter, descriptor/
NIC lifetime, queue admission, and elapsed I/O deadlines. Board sessions and
sensor drivers own device command/payload parsing, protocol correlation, and
reconciliation. One physical connection has one I/O owner; aliases must share
identity and effective policy. A fresh owner generation requires releasing the
old one. Do not add automatic reopening or command replay.

Total monotonic deadlines include queue wait and cannot be renewed by partial
progress. Keep cancellation callable while I/O waits, retain partial-publication
uncertainty, and do not equate an OS write with delivery or execution. Read the
[EtherCAT guide](ethercat/README.md) before changing its worker or paired adapters.

## Verification

Maintain test-only capability fakes without exporting concrete implementation
headers. Run `//robot/comm/testing:transport_boundary_test`, transport contract
tests, and affected serial/adapter tests in Docker. PTYs exercise disconnect,
queue expiry, cancellation, alias ownership, and framing without a physical port.
Physical USB/UART/NIC timing requires separate deliberate qualification.
